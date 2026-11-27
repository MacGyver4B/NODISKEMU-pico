/* NODISKEMU - disk imager in controller mode (X-IMG), see ximg.h
 *
 * Lessons from the PET program and the simulation, kept here:
 * - "I" before closing after a restore: otherwise DOS writes its cached
 *   BAM of the old disk over the freshly written header/BAM block (the
 *   track mode writes past the BAM handling of DOS as well).
 * - "N" closes all channels in the drive: reopen the "#" channel afterwards.
 * - The status number is parsed leniently (the emulated 8250 with an 8050
 *   disk returns "2=" on track 78).
 * - The command channel is opened like the PET does (OPEN 15,dev,15,"I0").
 * - Track mode: no ATN between M-E and the end of the fast transfer, the
 *   drive code runs with interrupts off and waits for the handshake.
 *
 * Placeholder: X-IMG works on sector level (job queue), damaged sectors end
 * up as error codes. Reading damaged tracks raw (2031 from the head, 2040 to
 * 8250 via controller code started with an $E0 job) is not planned; flux
 * tools do that better.
 */
#include <string.h>
#include "ximg.h"

#ifdef __AVR__
#  include <avr/pgmspace.h>
#  define XIMG_PROGMEM PROGMEM
#  define XIMG_ROMBYTE(p) pgm_read_byte(p)
#else
#  define XIMG_PROGMEM
#  define XIMG_ROMBYTE(p) (*(p))
#endif
#include "ximg_drive.h"

#define CH_BUF 2                /* direct access channel "#" */
#define CH_CMD 15

/* track mode: free DOS buffers used as ring (code in 3-6, job 0 = ID),
   high bytes from BUFIND (identical in DOS 2.1 and 2.7) */
static const uint8_t ring_job[7] = { 1, 2, 7, 8, 9, 10, 11 };
static const uint8_t ring_buf[7] = { 0x12, 0x13, 0x30, 0x31, 0x32, 0x33, 0x40 };

/* ---------------------------------------------------------------- tables */

uint8_t ximg_sectors(uint8_t type, uint8_t t)
{
    if (type == XIMG_D60 || type == XIMG_D90) {
        /* hard disk: track = cylinder, 32 sectors per head, sector = head*32+s */
        return type == XIMG_D60 ? 4 * 32 : 6 * 32;
    }
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
    case 5013504UL:                 return XIMG_D60;
    case 7520256UL:                 return XIMG_D90;
    case 349696UL: case 351062UL:   return XIMG_D71;
    default:                        return 0;
    }
}

/* the same sum as the drive code: carry runs through, no CLC per byte */
void ximg_checksum(const uint8_t *d, uint8_t out[2])
{
    uint16_t a = 0, c = 0, s2 = 0, t;
    uint8_t s1 = 0;
    uint16_t i;
    for (i = 0; i < 256; i++) {
        t = (uint16_t)(a + d[i] + c);
        a = t & 255;
        c = t >> 8;
        s1 = (uint8_t)a;
        t = (uint16_t)(a + s2 + c);
        s2 = t & 255;
        c = t >> 8;
        a = s1;
    }
    out[0] = s1;
    out[1] = (uint8_t)s2;
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
    /* lenient: up to two leading characters, '0'.. as digits */
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

/* track mode needs buffer 0 for the "#" channel: its job header carries the
   disk ID after every U1 */
static int open_buffer(const ximg_hal_t *h, ximg_t *x)
{
    static const uint8_t name[2] = { '#', '0' };
    if (h->send(h->ctx, x->dev, 0xf0 | CH_BUF, name, x->fast ? 2 : 1, 1)) {
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

/* DOS family from the reset vector of the drive ROM (M-R $FFFC) */
static uint8_t detect_dos(const ximg_hal_t *h, ximg_t *x)
{
    static const char mr[6] = { 'M', '-', 'R', (char)0xfc, (char)0xff, 2 };
    uint8_t v[2];
    int n;
    if (command(h, x, mr, 6)) {
        return XIMG_DOS_OTHER;
    }
    n = h->recv(h->ctx, x->dev, CH_CMD, v, 2, 2000);
    if (n == 2 && v[0] == 0x2b && v[1] == 0xd3) {
        return XIMG_DOS_21;
    }
    if (n == 2 && v[0] == 0xb7 && v[1] == 0xf2) {
        return XIMG_DOS_27;
    }
    status(h, x, 2000);                   /* drain what is left of the message */
    return XIMG_DOS_OTHER;
}

/* open command + buffer channel, detect DOS. tolerant: a blank disk must not
   stop us. The command channel is opened like the PET does
   (OPEN 15,dev,15,"I0"): the 2031 (DOS 2.6) ignores commands without it. */
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
    x->dos = detect_dos(h, x);
    x->fast = !x->opt_slow && h->fast_begin && h->fast_send && h->fast_recv && h->fast_end &&
              (x->dos == XIMG_DOS_21 || x->dos == XIMG_DOS_27);
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

/* D64 error table: code 1 = ok, otherwise DOS error - 18. On the first bad
   block the whole table is created (all ok), so the order of the blocks
   does not matter (track mode delivers them as they pass the head). */
static int etab_mark(ximg_t *x, const ximg_hal_t *h, uint16_t n, uint8_t code)
{
    if (x->type != XIMG_D64) {
        return 0;
    }
    if (!x->etab) {
        uint16_t i;
        if (code == 1) {
            return 0;
        }
        x->etab = (uint32_t)x->blocks * 256UL;
        memset(x->vbuf, 1, 256);
        for (i = 0; i < x->blocks; i += 256) {
            uint16_t k = (uint16_t)(x->blocks - i < 256 ? x->blocks - i : 256);
            if (h->img_write(h->ctx, x->etab + i, x->vbuf, k)) {
                return -1;
            }
        }
    }
    return h->img_write(h->ctx, x->etab + n, &code, 1);
}

/* -------------------------------------------------------------- track mode */

/* M-W of n bytes (from flash if rom) in pieces of 32 */
static int mem_write(const ximg_hal_t *h, ximg_t *x, uint16_t addr, const uint8_t *d, uint16_t n, int rom)
{
    char c[38];
    uint16_t i, k, j;
    for (i = 0; i < n; i += k) {
        k = (uint16_t)(n - i > 32 ? 32 : n - i);
        c[0] = 'M'; c[1] = '-'; c[2] = 'W';
        c[3] = (char)((addr + i) & 255);
        c[4] = (char)((addr + i) >> 8);
        c[5] = (char)k;
        for (j = 0; j < k; j++) {
            c[6 + j] = (char)(rom ? XIMG_ROMBYTE(d + i + j) : d[i + j]);
        }
        if (command(h, x, c, (uint8_t)(6 + k)) || status(h, x, 2000)) {
            return -1;
        }
    }
    return 0;
}

/* drive code + parameters into the drive (again after N: format uses the
   buffers). The disk ID comes from the header of job 0 (last U1). */
static int fast_load(const ximg_hal_t *h, ximg_t *x)
{
    uint8_t p[XIMG_P_IDJ + 1];
    uint8_t i;
    if (mem_write(h, x, XIMG_DRV_ORG, ximg_drv_code, sizeof(ximg_drv_code), 1)) {
        note(h, "drive code upload failed");
        return -1;
    }
    memset(p, 0, sizeof(p));
    p[XIMG_P_DRV] = x->drive;
    p[XIMG_P_R] = sizeof(ring_job);
    for (i = 0; i < sizeof(ring_job); i++) {
        p[XIMG_P_JOBN + i] = ring_job[i];
        p[XIMG_P_BUFH + i] = ring_buf[i];
    }
    p[XIMG_P_IDJ] = 0;
    return mem_write(h, x, XIMG_DRV_PAR, p, sizeof(p), 0);
}

/* one track in track mode.
   mode XIMG_P_READ: blocks -> image, XIMG_P_SURFACE: read and discard,
   XIMG_P_WRITE: image -> blocks with checksum test.
   returns the number of bad blocks, -1 = protocol/drive failure */
static int fast_track(const ximg_hal_t *h, ximg_t *x, uint8_t t, int mode)
{
    uint8_t ns = ximg_sectors(x->type, t);
    uint8_t p[2], i;
    char me[5];
    int bad = 0;
    uint16_t entry = mode == XIMG_P_WRITE ? XIMG_DRV_WRITE : XIMG_DRV_READ;

    p[0] = t;
    p[1] = ns;
    if (mem_write(h, x, XIMG_DRV_PAR + XIMG_P_T, p, 2, 0)) {
        return -1;
    }
    /* sectors always ascending (the job processor reads whatever pending
       sector passes the head first, so a skew gains nothing, measured) */
    me[0] = 'M'; me[1] = '-'; me[2] = 'E';
    me[3] = (char)(entry & 255);
    me[4] = (char)(entry >> 8);
    if (command(h, x, me, 5)) {
        return -1;
    }
    h->fast_begin(h->ctx);
    if (mode == XIMG_P_WRITE) {
        uint8_t sum[2];
        uint8_t nr = (uint8_t)((ns + 1) & ~1);
        for (i = 0; i < ns; i++) {
            if (h->img_read(h->ctx, ximg_offset(x->type, t, i), x->buf, 256)) {
                return -2;
            }
            ximg_checksum(x->buf, sum);
            if (h->fast_send(h->ctx, x->buf, 256, i ? 1000 : 2000) != 256 ||
                h->fast_send(h->ctx, sum, 2, 1000) != 2) {
                return -1;
            }
        }
        if (h->fast_recv(h->ctx, x->vbuf, nr, 5000) != nr) {
            return -1;
        }
        for (i = 0; i < ns; i++) {
            if (x->vbuf[i] != 1) {
                bad++;
            }
        }
    } else {
        for (i = 0; i < ns; i++) {
            uint8_t hd[2];
            if (h->fast_recv(h->ctx, hd, 2, 5000) != 2 ||
                h->fast_recv(h->ctx, x->buf, 256, 1000) != 256 || hd[0] >= ns) {
                return -1;
            }
            if (hd[1] != 1) {
                bad++;
            }
            if (mode == XIMG_P_READ) {
                uint32_t o = ximg_offset(x->type, t, hd[0]);
                if (hd[1] != 1) {
                    memset(x->buf, 0, 256);
                }
                if (h->img_write(h->ctx, o, x->buf, 256) ||
                    etab_mark(x, h, (uint16_t)(o / 256), hd[1])) {
                    return -2;
                }
            }
        }
    }
    h->fast_end(h->ctx);
    if (status(h, x, 2000)) {
        note(h, x->msg);
        return -1;
    }
    return bad;
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

    x->errors = 0;
    x->etab = 0;
    e = drive_open(h, x, 0);
    if (e) {
        return e;
    }
    /* format: 39/0 'C' = 8050/8250 (BAM 38/3 byte 5 tells 78/101 tracks),
       18/0 'A' = 2031/4040 (DOS 2). The last U1 also puts the disk ID into
       the header of job 0 for the track mode. */
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

    if (x->fast) {
        if (fast_load(h, x)) {
            e = XIMG_E_DRIVE;
            goto out;
        }
        for (t = 1; t <= x->tracks; t++) {
            int r;
            progress(h, XIMG_P_READ, t, x);
            r = fast_track(h, x, t, XIMG_P_READ);
            if (r == -2) {
                e = XIMG_E_IMAGE;
                goto out;
            }
            if (r < 0) {
                note(h, "track mode failed");
                e = XIMG_E_DRIVE;
                goto out;
            }
            x->errors = (uint16_t)(x->errors + r);
        }
    } else {
        for (t = 1; t <= x->tracks; t++) {
            progress(h, XIMG_P_READ, t, x);
            ns = ximg_sectors(x->type, t);
            for (s = 0; s < ns; s++, n++) {
                int r = read_block(h, x, t, s, x->buf);
                if (r < 0) {
                    note(h, "drive does not answer");
                    e = XIMG_E_NODRIVE;
                    goto out;
                }
                if (r) {
                    memset(x->buf, 0, 256);
                    x->errors++;
                }
                if (etab_mark(x, h, n, r ? err_code(r) : 1) ||
                    h->img_write(h->ctx, (uint32_t)n * 256UL, x->buf, 256)) {
                    e = XIMG_E_IMAGE;
                    goto out;
                }
            }
        }
    }
    if (x->etab) {
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
    if (x->fast) {
        /* new disk ID into job 0, drive code again (N used the buffers) */
        if (read_block(h, x, hdr_track(x->type), 0, x->vbuf) || fast_load(h, x)) {
            note(h, x->msg);
            return XIMG_E_DRIVE;
        }
        for (t = 1; t <= x->tracks; t++) {
            int r;
            progress(h, XIMG_P_SURFACE, t, x);
            r = fast_track(h, x, t, XIMG_P_SURFACE);
            if (r < 0) {
                return XIMG_E_DRIVE;
            }
            x->errors = (uint16_t)(x->errors + r);
        }
    } else {
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
    /* target disk (the U1 also puts its disk ID into the header of job 0:
       the track mode writes with the ID of the disk, not of the image) */
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
            read_block(h, x, ht, 0, x->vbuf);   /* ID of the disk into job 0 again */
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
    if (!fo && !x->fast) {               /* track mode always verifies */
        vf = x->opt_verify == 1 ||
             (x->opt_verify == 0 && (!h->ask || h->ask(h->ctx, XIMG_Q_VERIFY)));
    }

again:
    if (fo) {
        e = format_disk(x, h);
        if (e) {
            goto out;
        }
    } else if (x->fast && fast_load(h, x)) {
        e = XIMG_E_DRIVE;
        goto out;
    }
    x->errors = 0;
    if (x->fast) {
        for (t = 1; t <= x->tracks; t++) {
            int r;
            progress(h, XIMG_P_WRITE, t, x);
            r = fast_track(h, x, t, XIMG_P_WRITE);
            if (r > 0) {
                r = fast_track(h, x, t, XIMG_P_WRITE);   /* once more */
            }
            if (r == -2) {
                e = XIMG_E_IMAGE;
                goto out;
            }
            if (r < 0) {
                note(h, "track mode failed");
                e = XIMG_E_DRIVE;
                goto out;
            }
            x->errors = (uint16_t)(x->errors + r);
        }
    } else {
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
                            r = 25;       /* verify error */
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
