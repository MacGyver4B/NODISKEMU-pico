/* NODISKEMU - disk imager in controller mode (X-IMG), see ximg.h
 *
 * Stage 1: the same procedure as the PET program diskimager v2, block by
 * block with U1/U2. It is the reference for the speedups that follow
 * (interleave, fewer status reads, drive code via M-W/M-E).
 *
 * Lessons from the PET program, kept here:
 * - "I" before closing after a restore: otherwise DOS writes its cached
 *   BAM of the old disk over the freshly written header/BAM block.
 * - "N" closes all channels in the drive: reopen the "#" channel afterwards.
 * - The status number is parsed leniently (the emulated 8250 with an 8050
 *   disk returns "2=" on track 78).
 */
#include <string.h>
#include "ximg.h"

#define CH_BUF 2                /* direct access channel "#" */
#define CH_CMD 15

/* ---------------------------------------------------------------- tables */

uint8_t ximg_sectors(uint8_t type, uint8_t t)
{
    if (type == XIMG_D64) {
        return t <= 17 ? 21 : t <= 24 ? 19 : t <= 30 ? 18 : 17;
    }
    if (t > 77) {
        t -= 77;
    }
    return t <= 39 ? 29 : t <= 53 ? 27 : t <= 64 ? 25 : 23;
}

uint32_t ximg_offset(uint8_t type, uint8_t t, uint8_t s)
{
    uint32_t n = 0;
    uint8_t i;
    for (i = 1; i < t; i++) {
        n += ximg_sectors(type, i);
    }
    return (n + s) * 256UL;
}

uint8_t ximg_type_from_size(uint32_t size)
{
    switch (size) {
    case 174848UL: case 175531UL:   return XIMG_D64;
    case 533248UL: case 535331UL:   return XIMG_D80;
    case 1066496UL: case 1070662UL: return XIMG_D82;
    case 176640UL:                  return XIMG_D67;
    case 349696UL: case 351062UL:   return XIMG_D71;
    default:                        return 0;
    }
}

static void set_layout(ximg_t *x, uint8_t type)
{
    x->type = type;
    x->tracks = type == XIMG_D64 ? 35 : type == XIMG_D80 ? 77 : 154;
    x->blocks = type == XIMG_D64 ? 683 : type == XIMG_D80 ? 2083 : 4166;
}

/* header block and offset of the disk name in it */
static uint8_t hdr_track(uint8_t type) { return type == XIMG_D64 ? 18 : 39; }
static uint8_t hdr_name(uint8_t type)  { return type == XIMG_D64 ? 144 : 6; }

/* ------------------------------------------------------------- drive I/O */

static void note(const ximg_hal_t *h, const char *s)
{
    if (h->note) {
        h->note(h->ctx, s);
    }
}

/* append a decimal number */
static char *put_num(char *p, uint16_t v)
{
    char tmp[6];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n) {
        *p++ = tmp[--n];
    }
    return p;
}

static int command(const ximg_hal_t *h, ximg_t *x, const char *s, uint8_t len)
{
    return h->send(h->ctx, x->dev, 0x60 | CH_CMD, (const uint8_t *)s, len, 1);
}

/* read the error channel into x->status / x->msg. Returns the number,
   73 (power-on message) counts as 0. -1: drive does not answer */
static int status(const ximg_hal_t *h, ximg_t *x, uint32_t first_ms)
{
    int n = h->recv(h->ctx, x->dev, CH_CMD, (uint8_t *)x->msg, sizeof(x->msg) - 1, first_ms);
    int v = 0, i;
    if (n <= 0) {
        x->msg[0] = 0;
        x->status = 255;
        return -1;
    }
    while (n > 0 && (x->msg[n - 1] == 13 || x->msg[n - 1] == 10)) {
        n--;
    }
    x->msg[n] = 0;
    /* lenient: up to two leading characters, '0'.. as digits (the 8250 with
       a single-sided disk answers "2=" on track 78) */
    for (i = 0; i < 2 && i < n && x->msg[i] >= '0' && x->msg[i] <= '0' + 15; i++) {
        v = v * 10 + (x->msg[i] - '0');
    }
    x->status = (uint8_t)v;
    return v == 73 ? 0 : v;
}

/* "Ux:2 d t s" and status. op = '1' read, '2' write */
static int user_cmd(const ximg_hal_t *h, ximg_t *x, char op, uint8_t t, uint8_t s)
{
    char c[20], *p = c;
    *p++ = 'U'; *p++ = op; *p++ = ':';
    p = put_num(p, CH_BUF); *p++ = ' ';
    p = put_num(p, x->drive); *p++ = ' ';
    p = put_num(p, t); *p++ = ' ';
    p = put_num(p, s);
    if (command(h, x, c, (uint8_t)(p - c))) {
        return -1;
    }
    return status(h, x, 2000);
}

/* block t/s -> buf. Returns drive status (0 ok) */
static int read_block(const ximg_hal_t *h, ximg_t *x, uint8_t t, uint8_t s, uint8_t *buf)
{
    int e = user_cmd(h, x, '1', t, s);
    if (e) {
        return e;
    }
    if (h->recv(h->ctx, x->dev, CH_BUF, buf, 256, 2000) != 256) {
        return 99;
    }
    return 0;
}

/* buf -> block t/s */
static int write_block(const ximg_hal_t *h, ximg_t *x, uint8_t t, uint8_t s, const uint8_t *buf)
{
    static const char bp[] = "B-P 2 0";
    int e;
    if (command(h, x, bp, sizeof(bp) - 1)) {
        return -1;
    }
    e = status(h, x, 2000);
    if (e) {
        return e;
    }
    if (h->send(h->ctx, x->dev, 0x60 | CH_BUF, buf, 256, 0)) {
        return 99;
    }
    return user_cmd(h, x, '2', t, s);
}

static int open_buffer(const ximg_hal_t *h, ximg_t *x)
{
    static const uint8_t hash = '#';
    if (h->send(h->ctx, x->dev, 0xf0 | CH_BUF, &hash, 1, 1)) {
        return -1;
    }
    return status(h, x, 2000);
}

static int initialize(const ximg_hal_t *h, ximg_t *x)
{
    char c[2];
    c[0] = 'I';
    c[1] = (char)('0' + x->drive);
    if (command(h, x, c, 2)) {
        return -1;
    }
    return status(h, x, 5000);
}

/* open command + buffer channel. tolerant: a blank disk must not stop us.
   The command channel is opened like the PET does (OPEN 15,dev,15,"I0"):
   the 2031 (DOS 2.6) ignores commands on channel 15 without it. */
static int drive_open(const ximg_hal_t *h, ximg_t *x, int tolerant)
{
    char c[2];
    int e = status(h, x, 2000);           /* power-on message, drive present? */
    if (e < 0) {
        note(h, "drive does not answer");
        return XIMG_E_NODRIVE;
    }
    c[0] = 'I';
    c[1] = (char)('0' + x->drive);
    if (h->send(h->ctx, x->dev, 0xf0 | CH_CMD, (const uint8_t *)c, 2, 1)) {
        return XIMG_E_NODRIVE;
    }
    e = status(h, x, 5000);
    if (e > 19) {
        e = initialize(h, x);            /* second try (8250 with 8050 disk) */
    }
    if (e > 19 && !tolerant) {
        note(h, x->msg);
        return XIMG_E_DISK;
    }
    e = open_buffer(h, x);
    if (e > 19 || e < 0) {
        note(h, x->msg);
        return XIMG_E_DRIVE;
    }
    return XIMG_OK;
}

static void drive_close(const ximg_hal_t *h, ximg_t *x)
{
    h->close(h->ctx, x->dev, CH_BUF);
    h->close(h->ctx, x->dev, CH_CMD);
}

static void copy_name(ximg_t *x, const uint8_t *b)
{
    uint8_t i, o = hdr_name(x->type);
    for (i = 0; i < 16 && b[o + i] != 0xa0; i++) {
        x->name[i] = (char)b[o + i];
    }
    x->name[i] = 0;
    x->id[0] = (char)b[o + 18];
    x->id[1] = (char)b[o + 19];
    x->id[2] = 0;
}

static void progress(const ximg_hal_t *h, int phase, uint8_t t, ximg_t *x)
{
    if (h->progress) {
        h->progress(h->ctx, phase, t, x->tracks, x->errors);
    }
}

/* ------------------------------------------------------------------ backup */

/* D64 error table code from a DOS error number */
static uint8_t err_code(int e)
{
    return (e >= 20 && e <= 29) ? (uint8_t)(e - 18) : 4;
}

int ximg_backup(ximg_t *x, const ximg_hal_t *h)
{
    int e;
    uint8_t t, s, ns;
    uint16_t n = 0;
    uint32_t etab = 0;           /* != 0: offset of the D64 error table */

    x->errors = 0;
    e = drive_open(h, x, 0);
    if (e) {
        return e;
    }
    /* format: 39/0 'C' = 8050/8250 (BAM 38/3 byte 5 tells 78/101 tracks),
       18/0 'A' = 2031/4040 (DOS 2) */
    if (read_block(h, x, 39, 0, x->buf) == 0 && x->buf[2] == 'C') {
        x->type = XIMG_D80;
        copy_name(x, x->buf);
        if (read_block(h, x, 38, 3, x->buf)) {
            note(h, x->msg);
            e = XIMG_E_DISK;
            goto out;
        }
        if (x->buf[5] == 101) {
            set_layout(x, XIMG_D82);
        } else if (x->buf[5] == 78) {
            set_layout(x, XIMG_D80);
        } else {
            note(h, "unknown format (BAM)");
            e = XIMG_E_FORMAT;
            goto out;
        }
    } else if (read_block(h, x, 18, 0, x->buf) == 0 && x->buf[2] == 'A') {
        if (x->buf[3] & 0x80) {
            note(h, "D71 (1571) not supported yet");
            e = XIMG_E_FORMAT;
            goto out;
        }
        if (x->buf[165] == '1') {
            note(h, "DOS 1 (D67) not supported yet");
            e = XIMG_E_FORMAT;
            goto out;
        }
        set_layout(x, XIMG_D64);
        copy_name(x, x->buf);
    } else {
        note(h, "not a CBM disk");
        e = XIMG_E_DISK;
        goto out;
    }
    x->size = (uint32_t)x->blocks * 256UL;

    for (t = 1; t <= x->tracks; t++) {
        progress(h, XIMG_P_READ, t, x);
        ns = ximg_sectors(x->type, t);
        for (s = 0; s < ns; s++, n++) {
            int r = read_block(h, x, t, s, x->buf);
            if (r) {
                if (r < 0) {
                    note(h, "drive does not answer");
                    e = XIMG_E_NODRIVE;
                    goto out;
                }
                memset(x->buf, 0, 256);
                x->errors++;
                if (x->type == XIMG_D64) {
                    uint8_t c = err_code(r);
                    if (!etab) {
                        /* first error: all blocks before were fine */
                        uint8_t one[32];
                        uint16_t i;
                        etab = x->size;
                        memset(one, 1, sizeof(one));
                        for (i = 0; i < n; i += 32) {
                            uint16_t k = (uint16_t)(n - i < 32 ? n - i : 32);
                            if (h->img_write(h->ctx, etab + i, one, k)) {
                                e = XIMG_E_IMAGE;
                                goto out;
                            }
                        }
                    }
                    if (h->img_write(h->ctx, etab + n, &c, 1)) {
                        e = XIMG_E_IMAGE;
                        goto out;
                    }
                }
            } else if (etab) {
                static const uint8_t ok = 1;
                if (h->img_write(h->ctx, etab + n, &ok, 1)) {
                    e = XIMG_E_IMAGE;
                    goto out;
                }
            }
            if (h->img_write(h->ctx, (uint32_t)n * 256UL, x->buf, 256)) {
                e = XIMG_E_IMAGE;
                goto out;
            }
        }
    }
    if (etab) {
        x->size += x->blocks;
    }
    e = x->errors ? XIMG_E_ERRORS : XIMG_OK;
out:
    drive_close(h, x);
    return e;
}

/* ----------------------------------------------------------------- restore */

/* format with surface test. 0 ok */
static int format_disk(ximg_t *x, const ximg_hal_t *h)
{
    char c[40], *p = c;
    uint8_t t, s, ns, i;
    int e;

    progress(h, XIMG_P_FORMAT, 0, x);
    *p++ = 'N';
    *p++ = (char)('0' + x->drive);
    *p++ = ':';
    for (i = 0; x->name[i]; i++) {
        *p++ = x->name[i];
    }
    *p++ = ',';
    *p++ = x->id[0];
    *p++ = x->id[1];
    if (command(h, x, c, (uint8_t)(p - c))) {
        return XIMG_E_NODRIVE;
    }
    e = status(h, x, 600000UL);           /* formatting takes a while */
    if (e) {
        note(h, x->msg);
        return XIMG_E_DRIVE;
    }
    /* N closes all channels in the drive: reopen the buffer channel */
    h->close(h->ctx, x->dev, CH_BUF);
    e = open_buffer(h, x);
    if (e) {
        note(h, x->msg);
        return XIMG_E_DRIVE;
    }
    x->errors = 0;
    for (t = 1; t <= x->tracks; t++) {
        progress(h, XIMG_P_SURFACE, t, x);
        ns = ximg_sectors(x->type, t);
        for (s = 0; s < ns; s++) {
            e = user_cmd(h, x, '1', t, s);
            if (e < 0) {
                return XIMG_E_NODRIVE;
            }
            if (e) {
                x->errors++;
            }
        }
    }
    if (x->errors) {
        note(h, "bad blocks - use another disk");
        return XIMG_E_BADDISK;
    }
    return XIMG_OK;
}

int ximg_restore(ximg_t *x, const ximg_hal_t *h)
{
    int e, dc, lm = 0, uf = 0, mf = 0, fo = 0, vf = 1, retried = 0;
    uint8_t t, s, ns, ht;
    uint16_t n;

    if (x->type == XIMG_D67 || x->type == XIMG_D71) {
        note(h, "D67/D71 not supported yet");
        return XIMG_E_FORMAT;
    }
    if (x->type != XIMG_D64 && x->type != XIMG_D80 && x->type != XIMG_D82) {
        return XIMG_E_FORMAT;
    }
    set_layout(x, x->type);
    ht = hdr_track(x->type);
    if (h->img_read(h->ctx, ximg_offset(x->type, ht, 0), x->buf, 256)) {
        return XIMG_E_IMAGE;
    }
    copy_name(x, x->buf);

    e = drive_open(h, x, 1);
    if (e) {
        return e;
    }
    /* which drive: track 36 illegal -> 2031/4040, track 78 illegal -> 8050 */
    dc = XIMG_D82;
    if (user_cmd(h, x, '1', 36, 0) == 66) {
        dc = XIMG_D64;
    } else if (user_cmd(h, x, '1', 78, 0) == 66) {
        dc = XIMG_D80;
    }
    if (x->type != dc) {
        if (x->type == XIMG_D80 && dc == XIMG_D82) {
            lm = 1;                       /* 8250: only onto 8050 disks */
        } else {
            note(h, "image does not fit this drive");
            e = XIMG_E_MISMATCH;
            goto out;
        }
    }
    /* target disk */
    e = read_block(h, x, ht, 0, x->vbuf);
    if (e >= 20 && e <= 29) {
        uf = 1;                           /* unformatted */
    } else if (e) {
        note(h, x->msg);
        e = XIMG_E_DRIVE;
        goto out;
    } else if (x->type == XIMG_D64) {
        mf = x->vbuf[2] != 'A';
    } else {
        mf = x->vbuf[2] != 'C';
        if (!mf) {
            if (read_block(h, x, 38, 3, x->vbuf)) {
                mf = 1;
            } else {
                mf = x->vbuf[5] != (x->type == XIMG_D80 ? 78 : 101);
            }
        }
    }
    if (lm && (uf || mf)) {
        note(h, "disk not in 8050 format (8250 formats D82 only)");
        e = XIMG_E_MISMATCH;
        goto out;
    }
    if (uf) {
        note(h, "disk unformatted: will be formatted");
        fo = 1;
    } else if (mf) {
        if (!h->ask || !h->ask(h->ctx, XIMG_Q_FORMAT_OTHER)) {
            e = XIMG_E_ABORT;
            goto out;
        }
        fo = 1;
    } else if (!lm) {
        fo = x->opt_format == 1 ||
             (x->opt_format == 0 && h->ask && h->ask(h->ctx, XIMG_Q_FORMAT));
    }
    if (!fo) {
        vf = x->opt_verify == 1 ||
             (x->opt_verify == 0 && (!h->ask || h->ask(h->ctx, XIMG_Q_VERIFY)));
    }

again:
    if (fo) {
        e = format_disk(x, h);
        if (e) {
            goto out;
        }
    }
    x->errors = 0;
    n = 0;
    for (t = 1; t <= x->tracks; t++) {
        progress(h, XIMG_P_WRITE, t, x);
        ns = ximg_sectors(x->type, t);
        for (s = 0; s < ns; s++, n++) {
            int r, tries;
            if (h->img_read(h->ctx, (uint32_t)n * 256UL, x->buf, 256)) {
                e = XIMG_E_IMAGE;
                goto out;
            }
            for (tries = 0; tries < 2; tries++) {
                r = write_block(h, x, t, s, x->buf);
                if (r < 0) {
                    e = XIMG_E_NODRIVE;
                    goto out;
                }
                if (r == 0 && vf) {
                    r = read_block(h, x, t, s, x->vbuf);
                    if (r == 0 && memcmp(x->buf, x->vbuf, 256)) {
                        r = 25;           /* verify error */
                    }
                }
                if (r == 0) {
                    break;
                }
            }
            if (r) {
                x->errors++;
            }
        }
    }
    /* drop the cached BAM of the old disk before closing */
    initialize(h, x);
    if (x->errors && !fo && !retried && h->ask && h->ask(h->ctx, XIMG_Q_RETRY_FORMAT)) {
        fo = 1;
        vf = 1;
        retried = 1;
        goto again;
    }
    e = x->errors ? XIMG_E_ERRORS : XIMG_OK;
out:
    drive_close(h, x);
    return e;
}
