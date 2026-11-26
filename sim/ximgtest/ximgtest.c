/*
 * ximgtest - host test program for the X-IMG controller module (src/ximg.c).
 *
 * Hangs as a tool on a simlink IEEE-488 bus and plays the bus controller
 * (the role petSD+ takes in controller mode). The drives come from VICE
 * (xpet with true drive emulation; the PET itself just sits at READY).
 * The image is a plain file on the host.
 *
 *   ximgtest [-v] [-d dev] [-D drive] [-f a|y|n] [-V a|y|n] [-y] bus backup  image
 *   ximgtest [-v] [-d dev] [-D drive] [-f a|y|n] [-V a|y|n] [-y] bus restore image
 *
 *   -f  format on restore: ask (default: no), y = always, n = never
 *   -V  verify on restore: ask (default: yes), y, n
 *   -y  answer all questions with yes
 *
 * Times are simulated bus time (simlink picoseconds), so they are exact and
 * reproducible, independent of the host speed.
 *
 * The IEEE controller primitives follow relay.c (-r) from VICE-LRE-SIMLINK.
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "simlink.h"
#include "ximg.h"
#include "fast4040.h"
#include "drive4040.h"

#define STEP 1000000ULL             /* 1 us in ps */

static volatile sig_atomic_t stop = 0;
static int verbose = 0;
static unsigned gap_us = 100;        /* ATN high after UNLISTEN */
static uint64_t t = 0;              /* own time (ps), relative to off */

static simlink_t *sl;
static int slot;
static uint64_t off;
static uint8_t own_pull, own_data, in, in_data;

/* statistics */
static unsigned long n_cmd, n_bytes_out, n_bytes_in;

static void on_sig(int s) { (void)s; stop = 1; }

static void quit(int code)
{
    if (sl) {
        simlink_detach(sl, slot);
        simlink_close(sl, 0);
    }
    exit(code);
}

static void rd(void)
{
    in = simlink_read(sl, slot, t + off, &in_data);
    if (stop) {
        quit(1);
    }
}

static void tick(void) { t += STEP; rd(); }

static uint8_t extra_pull = 0;      /* e.g. REN held low like the PET */

static void set(uint8_t pull, uint8_t data)
{
    pull = (uint8_t)(pull | extra_pull);
    if (pull != own_pull || data != own_data) {
        own_pull = pull;
        own_data = data;
        simlink_set(sl, slot, t + off, pull, data);
    }
}

/* wait for a change of a line in mask, at most timeout_us (0 = no limit) */
static void wait_change(uint8_t mask, uint64_t timeout_us)
{
    uint64_t tc, dl = timeout_us ? t + off + timeout_us * STEP : 0;
    int ch = simlink_wait_change(sl, slot, t + off, mask, STEP, dl, &tc);
    uint64_t tn = (ch ? tc + STEP : dl) - off;
    if (tn < t) {
        tn = t;
    }
    simlink_wait_end(sl, slot, tn + off);
    t = tn;
    rd();
}

static void wait_us(uint64_t n)
{
    if (n) {                        /* like relay.c: one read at the end */
        t += (n - 1) * STEP;
        tick();
    }
}

#define LOW(m) (in & (m))

/* until (bus & m) == want; 0 ok, -1 timeout */
static int bwait(uint8_t m, uint8_t want, uint64_t timeout_us)
{
    uint64_t dl = timeout_us ? t + timeout_us * STEP : 0;
    tick();
    for (;;) {
        if ((in & m) == want) {
            return 0;
        }
        if (dl && t >= dl) {
            return -1;
        }
        wait_change(m, dl ? (dl - t) / STEP : 0);
    }
}

static int send_byte(uint8_t b, int eoi)
{
    uint8_t base = (uint8_t)(own_pull & SL_IEEE_ATN);
    set(base, 0);
    if (bwait(SL_IEEE_NRFD, 0, 0) < 0) {
        return -1;
    }
    set((uint8_t)(base | (eoi ? SL_IEEE_EOI : 0)), b);
    wait_us(2);
    set((uint8_t)(base | SL_IEEE_DAV | (eoi ? SL_IEEE_EOI : 0)), b);
    if (bwait(SL_IEEE_NDAC, 0, 65000) < 0) {
        set(base, 0);
        return -1;
    }
    set(base, 0);
    return 0;
}

static int atn(uint8_t b1, int b2)
{
    set(SL_IEEE_ATN, 0);
    wait_us(100);
    if (!LOW(SL_IEEE_NDAC) && !LOW(SL_IEEE_NRFD)) {
        if (verbose) {
            fprintf(stderr, "%10.3f ms ATN $%02X: no device (bus %02x)\n", t / 1e9, b1, in);
        }
        set(0, 0);
        return -1;
    }
    if (send_byte(b1, 0) < 0 || (b2 >= 0 && send_byte((uint8_t)b2, 0) < 0)) {
        set(0, 0);
        return -1;
    }
    return 0;
}

static void atn_end(void)
{
    wait_us(2);
    set(0, 0);
    wait_us(20);
}

/* ------------------------------------------------------------- HAL */

static int hal_send(void *ctx, uint8_t dev, uint8_t sa, const uint8_t *d, uint16_t n, uint8_t eoi)
{
    uint16_t i;
    int r = 0;
    (void)ctx;
    n_cmd++;
    if (verbose > 1) {
        fprintf(stderr, "%10.3f ms LISTEN %d $%02X %u bytes:", t / 1e9, dev, sa, n);
        for (i = 0; i < n && i < 40; i++) {
            fputc(d[i] >= 32 && d[i] < 127 ? d[i] : '.', stderr);
        }
        fputc('\n', stderr);
    }
    if (atn((uint8_t)(0x20 + dev), sa) < 0) {
        return -1;
    }
    atn_end();
    for (i = 0; i < n; i++) {
        if (send_byte(d[i], eoi && i + 1 == n) < 0) {
            r = -1;
            break;
        }
    }
    n_bytes_out += i;
    if (atn(0x3f, -1) == 0) {
        atn_end();
        wait_us(gap_us);
    }
    return r;
}

static int hal_recv(void *ctx, uint8_t dev, uint8_t ch, uint8_t *buf, uint16_t max, uint32_t first_ms)
{
    int n = 0, eoi = 0;
    (void)ctx;
    n_cmd++;
    if (atn((uint8_t)(0x40 + dev), 0x60 | ch) < 0) {
        return -1;
    }
    set(SL_IEEE_ATN | SL_IEEE_NRFD | SL_IEEE_NDAC, 0);
    wait_us(2);
    set(SL_IEEE_NRFD | SL_IEEE_NDAC, 0);
    while (!eoi && n < max) {
        set(SL_IEEE_NDAC, 0);
        if (bwait(SL_IEEE_DAV, SL_IEEE_DAV, n == 0 ? (uint64_t)first_ms * 1000 : 65000) < 0) {
            break;
        }
        set(SL_IEEE_NRFD | SL_IEEE_NDAC, 0);
        buf[n++] = in_data;
        eoi = LOW(SL_IEEE_EOI) ? 1 : 0;
        set(SL_IEEE_NRFD, 0);
        if (bwait(SL_IEEE_DAV, 0, 65000) < 0) {
            break;
        }
        set(SL_IEEE_NRFD | SL_IEEE_NDAC, 0);
    }
    set(SL_IEEE_ATN, 0);
    wait_us(100);
    send_byte(0x5f, 0);
    atn_end();
    n_bytes_in += n;
    if (verbose > 1 && ch == 15) {
        fprintf(stderr, "%10.3f ms status: %.*s\n", t / 1e9, n > 0 && buf[n - 1] == 13 ? n - 1 : n, buf);
    }
    return n;
}

static void hal_close(void *ctx, uint8_t dev, uint8_t ch)
{
    (void)ctx;
    if (atn((uint8_t)(0x20 + dev), 0xe0 | ch) == 0) {
        send_byte(0x3f, 0);
        atn_end();
    }
}


/* ---------------------------------------------------- fast protocol (test) */

/* receive n bytes: drive toggles DAV per byte, we toggle NRFD as ack.
   The expected DAV state runs through the whole transfer (fast_start):
   the drive may already have put the next byte on the bus. */
static uint8_t fast_dav, fast_nrfd;
static int fast_first;                 /* first byte after M-E: the drive may take a while */

static void fast_start(void)
{
    fast_dav = 0;                   /* DAV released */
    fast_nrfd = 0;                  /* NRFD released */
    fast_first = 1;
    set(0, 0);
}

/* send n bytes: we toggle DAV per byte (data inverted, the drive stores the
   raw port value), the drive acks by toggling NRFD */
static int fast_send_t(const uint8_t *d, int n, uint64_t first_us)
{
    int i;
    for (i = 0; i < n; i++) {
        set((uint8_t)(own_pull ^ SL_IEEE_DAV), (uint8_t)~d[i]);
        fast_nrfd ^= SL_IEEE_NRFD;
        if (bwait(SL_IEEE_NRFD, fast_nrfd, (fast_first || i == 0) ? first_us : 1000000) < 0) {
            fprintf(stderr, "fast_send: no ack for byte %d (bus %02x)\n", i, in);
            return i;
        }
        fast_first = 0;
    }
    set(own_pull, 0);               /* release the data lines */
    return n;
}

static int fast_send(const uint8_t *d, int n)
{
    return fast_send_t(d, n, 2000000);
}

static void fletcher(const uint8_t *d, uint8_t out[2])
{
    unsigned a = 0, c = 0, s2 = 0, s1 = 0, t;
    int i;
    for (i = 0; i < 256; i++) {
        t = a + d[i] + c; a = t & 255; c = t >> 8; s1 = a;
        t = a + s2 + c; s2 = t & 255; c = t >> 8; a = s1;
    }
    out[0] = (uint8_t)s1;
    out[1] = (uint8_t)s2;
}

/* after the last ack: give the drive time to see it before ATN (with ATN
   the 4040 hardware pulls NRFD/NDAC itself, the ack would get lost) */
static void fast_end(void)
{
    wait_us(200);
}

static int fast_recv(uint8_t *buf, int n, uint64_t first_us)
{
    uint8_t last = fast_dav;
    int i;
    for (i = 0; i < n; i++) {
        if (bwait(SL_IEEE_DAV, (uint8_t)(last ^ SL_IEEE_DAV), i ? 10000 : first_us) < 0) {
            break;
        }
        last ^= SL_IEEE_DAV;
        fast_dav = last;
        buf[i] = FAST4040_RAW ? (uint8_t)~in_data : in_data;
        set((uint8_t)(own_pull ^ SL_IEEE_NRFD), 0);
    }
    return i;
}

static int fast_test(uint8_t dev, const char *imgname)
{
    uint8_t b[300], ref[256];
    char c[48];
    int i, n, k;
    uint64_t t1;
    FILE *f = fopen(imgname, "rb");
    if (!f || fseek(f, (long)ximg_offset(XIMG_D64, 18, 0), SEEK_SET) || fread(ref, 1, 256, f) != 256) {
        fprintf(stderr, "image %s\n", imgname);
        return 1;
    }
    fclose(f);
    verbose = 2;
    hal_recv(NULL, dev, 15, b, 41, 2000);
    hal_send(NULL, dev, 0xff, (const uint8_t *)"I0", 2, 1);
    hal_recv(NULL, dev, 15, b, 41, 5000);
    hal_send(NULL, dev, 0xf2, (const uint8_t *)"#4", 2, 1);
    hal_recv(NULL, dev, 15, b, 41, 2000);
    hal_send(NULL, dev, 0xf3, (const uint8_t *)"#5", 2, 1);
    hal_recv(NULL, dev, 15, b, 41, 2000);
    hal_send(NULL, dev, 0x6f, (const uint8_t *)"U1:2 0 18 0", 11, 1);
    hal_recv(NULL, dev, 15, b, 41, 2000);
    for (i = 0; i < (int)sizeof(fast4040_send); i += k) {
        k = (int)sizeof(fast4040_send) - i;
        if (k > 32) {
            k = 32;
        }
        memcpy(c, "M-W", 3);
        c[3] = (char)((0x2200 + i) & 255);
        c[4] = (char)((0x2200 + i) >> 8);
        c[5] = (char)k;
        memcpy(c + 6, fast4040_send + i, (size_t)k);
        hal_send(NULL, dev, 0x6f, (const uint8_t *)c, (uint16_t)(6 + k), 1);
        hal_recv(NULL, dev, 15, b, 41, 2000);
    }
    verbose = 1;
    t1 = t;
    hal_send(NULL, dev, 0x6f, (const uint8_t *)"M-E\x00\x22", 5, 1);
    {
        uint64_t t2 = t;
        fast_start();
        n = fast_recv(b, 256, 2000000);
        fast_end();
        fprintf(stderr, "fast: %d bytes in %.3f ms (M-E command %.3f ms), %.2f us/byte\n", n,
                (t - t2) / 1e9, (t2 - t1) / 1e9, n ? (t - t2) / 1e6 / n : 0.0);
    }
    for (i = 0; i < 5; i++) {
        fprintf(stderr, "%10.3f ms bus %02x data %02x own %02x\n", t / 1e9, in, in_data, own_pull);
        wait_us(1000);
    }
    fprintf(stderr, "compare with image 18/0: %s\n", n == 256 && !memcmp(b, ref, 256) ? "IDENTICAL" : "DIFFERENT");
    verbose = 2;
    hal_recv(NULL, dev, 15, (uint8_t *)c, 41, 2000);
    fprintf(stderr, "compare with image 18/0: %s\n", n == 256 && !memcmp(b, ref, 256) ? "IDENTICAL" : "DIFFERENT");
    if (n == 256 && memcmp(b, ref, 256)) {
        for (i = 0; i < 16; i++) {
            fprintf(stderr, "%02x/%02x ", b[i], ref[i]);
        }
        fputc('\n', stderr);
    }
    return 0;
}


static int mw(uint8_t dev, uint16_t addr, const uint8_t *d, int n)
{
    char c[48];
    uint8_t st[42];
    int i, k;
    for (i = 0; i < n; i += k) {
        k = n - i > 32 ? 32 : n - i;
        memcpy(c, "M-W", 3);
        c[3] = (char)((addr + i) & 255);
        c[4] = (char)((addr + i) >> 8);
        c[5] = (char)k;
        memcpy(c + 6, d + i, (size_t)k);
        hal_send(NULL, dev, 0x6f, (const uint8_t *)c, (uint16_t)(6 + k), 1);
        hal_recv(NULL, dev, 15, st, 41, 2000);
        if (st[0] != '0') {
            return -1;
        }
    }
    return 0;
}

/* whole disk with the drive code (4040, D64): read (backup) or write+verify (restore) */
static int d4040_setup(uint8_t dev, uint8_t par[25], int type)
{
    static const uint8_t bufind[12] = { 0x11, 0x12, 0x13, 0x20, 0x21, 0x22, 0x23, 0x30, 0x31, 0x32, 0x33, 0x40 };
    static const uint8_t ring[] = { 1, 2, 7, 8, 9, 10, 11 };
    uint8_t b[64];
    int i;

    hal_recv(NULL, dev, 15, b, 41, 2000);
    hal_send(NULL, dev, 0xff, (const uint8_t *)"I0", 2, 1);
    hal_recv(NULL, dev, 15, b, 41, 5000);
    hal_send(NULL, dev, 0xf2, (const uint8_t *)"#0", 2, 1);
    hal_recv(NULL, dev, 15, b, 41, 2000);
    /* disk ID into header of job 0 */
    hal_send(NULL, dev, 0x6f, (const uint8_t *)(type == XIMG_D64 ? "U1:2 0 18 0" : "U1:2 0 39 0"), 11, 1);
    hal_recv(NULL, dev, 15, b, 41, 2000);
    if (mw(dev, D4040_ORG, d4040_code, (int)sizeof(d4040_code)) < 0) {
        fprintf(stderr, "M-W failed\n");
        return -1;
    }
    memset(par, 0, 25);
    par[3] = (uint8_t)sizeof(ring);
    for (i = 0; i < (int)sizeof(ring); i++) {
        par[4 + i] = ring[i];
        par[14 + i] = bufind[ring[i]];
    }
    par[24] = 0;
    return 0;
}

static int track_test(uint8_t dev, const char *name, int write)
{
    uint8_t par[25], b[300];
    int type = getenv("XIMG_TYPE") ? atoi(getenv("XIMG_TYPE")) : XIMG_D64;
    int tracks = type == XIMG_D64 ? 35 : type == XIMG_D80 ? 77 : 154;
    int blocks = type == XIMG_D64 ? 683 : type == XIMG_D80 ? 2083 : 4166;
    uint8_t *img = calloc((size_t)blocks, 256);
    int i, n, total = 0, bad = 0;
    uint64_t t0, tt;
    FILE *f;

    if (write) {
        f = fopen(name, "rb");
        if (!f || fread(img, 256, (size_t)blocks, f) != (size_t)blocks) {
            fprintf(stderr, "image %s\n", name);
            return 1;
        }
        fclose(f);
    }
    if (d4040_setup(dev, par, type) < 0) {
        return 1;
    }
    t0 = t;
    for (int tr = 1; tr <= tracks; tr++) {
        int ns = ximg_sectors((uint8_t)type, (uint8_t)tr);
        char me[6] = { 'M', '-', 'E', 0, 0, 0 };
        uint16_t entry = write ? D4040_WRITE : D4040_READ;
        tt = t;
        par[0] = (uint8_t)tr;
        par[1] = (uint8_t)ns;
        if (mw(dev, D4040_PAR, par, tr == 1 ? 25 : 2) < 0) {
            fprintf(stderr, "M-W par failed\n");
            return 1;
        }
        me[3] = (char)(entry & 255);
        me[4] = (char)(entry >> 8);
        hal_send(NULL, dev, 0x6f, (const uint8_t *)me, 5, 1);
        fast_start();
        if (write) {
            uint8_t res[30];
            int nr = (ns + 1) & ~1;
            for (int k = 0; k < ns; k++) {
                uint8_t *blk;
                i = k;
                blk = img + ximg_offset((uint8_t)type, (uint8_t)tr, (uint8_t)i);
                uint8_t sum[2];
                fletcher(blk, sum);
                if (getenv("XIMG_BADSUM") && tr == atoi(getenv("XIMG_BADSUM")) && i == 3) {
                    sum[1] ^= 0x40;     /* test: verify must report this sector */
                }
                if (fast_send(blk, 256) != 256 || fast_send(sum, 2) != 2) {
                    fprintf(stderr, "track %d: sending sector %d failed\n", tr, i);
                    return 1;
                }
                total++;
            }
            n = fast_recv(res, nr, 5000000);
            if (n != nr) {
                fprintf(stderr, "track %d: only %d result bytes\n", tr, n);
                return 1;
            }
            for (i = 0; i < ns; i++) {
                if (res[i] != 1) {
                    bad++;
                    fprintf(stderr, "track %d sector %d: result %d\n", tr, i, res[i]);
                }
            }
        } else {
            for (i = 0; i < ns; i++) {
                n = fast_recv(b, 258, 5000000);
                if (n != 258) {
                    fprintf(stderr, "track %d: record %d: only %d bytes\n", tr, i, n);
                    return 1;
                }
                if (b[0] >= ns) {
                    fprintf(stderr, "track %d: bad sector %d\n", tr, b[0]);
                    return 1;
                }
                if (b[1] != 1) {
                    bad++;
                    fprintf(stderr, "track %d sector %d: job result %d\n", tr, b[0], b[1]);
                }
                memcpy(img + ximg_offset((uint8_t)type, (uint8_t)tr, b[0]), b + 2, 256);
                total++;
            }
        }
        fast_end();
        hal_recv(NULL, dev, 15, b, 41, 2000);
        if (verbose && (verbose > 1 || tr % 10 == 1 || tr == tracks)) {
            fprintf(stderr, "  track %2d: %2d sectors %.1f ms, status %.10s\n", tr, ns, (t - tt) / 1e9, b);
        }
    }
    if (getenv("XIMG_HOLD")) {
        fprintf(stderr, "hold\n");
        sleep(40);
    }
    fprintf(stderr, "%s D%d: %d blocks, %d bad, %.3f s bus time, %.2f KB/s\n", write ? "write+verify" : "read",
            type, total, bad, (t - t0) / 1e12, blocks * 256.0 / 1024 / ((t - t0) / 1e12));
    if (!write) {
        f = fopen(name, "wb");
        if (f) {
            fwrite(img, 256, (size_t)blocks, f);
            fclose(f);
        }
    }
    return 0;
}

static FILE *img;

static int hal_img_read(void *ctx, uint32_t o, uint8_t *d, uint16_t n)
{
    (void)ctx;
    return fseek(img, (long)o, SEEK_SET) || fread(d, 1, n, img) != n;
}

static int hal_img_write(void *ctx, uint32_t o, const uint8_t *d, uint16_t n)
{
    (void)ctx;
    return fseek(img, (long)o, SEEK_SET) || fwrite(d, 1, n, img) != n;
}

static int opt_yes = 0;

static int hal_ask(void *ctx, int q)
{
    static const char *const txt[] = { "", "target disk has another format: format?",
        "format target disk?", "verify?", "errors: format, test, rewrite?" };
    (void)ctx;
    fprintf(stderr, "question: %s -> %s\n", txt[q], opt_yes ? "yes" : "no");
    return opt_yes;
}

static uint64_t t_track;
static int last_phase = 0;

static void hal_progress(void *ctx, int phase, uint8_t tr, uint8_t trs, uint16_t err)
{
    static const char *const ph[] = { "", "read", "write", "surface", "format" };
    (void)ctx;
    if (verbose && tr > 1 && phase == last_phase) {
        fprintf(stderr, "  %s track %3u/%u done: %.3f s, errors %u\n", ph[phase], tr - 1, trs,
                (t - t_track) / 1e12, err);
    }
    if (phase != last_phase) {
        fprintf(stderr, "%10.3f s %s\n", t / 1e12, ph[phase]);
    }
    last_phase = phase;
    t_track = t;
}

static void hal_note(void *ctx, const char *m)
{
    (void)ctx;
    fprintf(stderr, "note: %s\n", m);
}

static void hal_fast_begin(void *ctx) { (void)ctx; fast_start(); }
static void hal_fast_end(void *ctx) { (void)ctx; fast_end(); }

static int hal_fast_send(void *ctx, const uint8_t *d, uint16_t n, uint32_t first_ms)
{
    (void)ctx;
    return fast_send_t(d, n, (uint64_t)first_ms * 1000);
}

static int hal_fast_recv(void *ctx, uint8_t *d, uint16_t n, uint32_t first_ms)
{
    (void)ctx;
    return fast_recv(d, n, (uint64_t)first_ms * 1000);
}

int main(int argc, char **argv)
{
    static ximg_t x;
    ximg_hal_t h = { NULL, hal_send, hal_recv, hal_close, hal_img_read, hal_img_write,
                     hal_ask, hal_progress, hal_note,
                     hal_fast_begin, hal_fast_send, hal_fast_recv, hal_fast_end };
    int c, r, restore;
    double boot_s = 3.0;
    uint64_t t0;

    x.dev = 8;
    while ((c = getopt(argc, argv, "vd:D:f:V:yb:g:S")) != -1) {
        switch (c) {
        case 'v': verbose++; break;
        case 'd': x.dev = (uint8_t)atoi(optarg); break;
        case 'D': x.drive = (uint8_t)atoi(optarg); break;
        case 'f': x.opt_format = optarg[0] == 'y' ? 1 : optarg[0] == 'n' ? 2 : 0; break;
        case 'V': x.opt_verify = optarg[0] == 'y' ? 1 : optarg[0] == 'n' ? 2 : 0; break;
        case 'y': opt_yes = 1; break;
        case 'b': boot_s = atof(optarg); break;
        case 'g': gap_us = (unsigned)atoi(optarg); break;
        case 'S': x.opt_slow = 1; break;
        default: goto usage;
        }
    }
    if (optind + 3 == argc && !strcmp(argv[optind + 1], "track")) {
        restore = -3;
        goto fastmode;
    }
    if (optind + 3 == argc && !strcmp(argv[optind + 1], "wtrack")) {
        restore = -4;
        goto fastmode;
    }
    if (optind + 3 == argc && !strcmp(argv[optind + 1], "fast")) {
        restore = -2;
        goto fastmode;
    }
    if (optind + 3 == argc && !strcmp(argv[optind + 1], "cmd")) {
        goto cmdmode;
    }
    if (optind + 3 != argc || (strcmp(argv[optind + 1], "backup") && strcmp(argv[optind + 1], "restore"))) {
usage:
        fprintf(stderr, "usage: ximgtest [-v] [-d dev] [-D drive] [-f a|y|n] [-V a|y|n] [-y] [-b boot_s] bus backup|restore image\n");
        return 2;
    }
    restore = !strcmp(argv[optind + 1], "restore");
    if (0) {
cmdmode:
        restore = -1;
    }
    if (0) {
fastmode:
        if (restore == -3) {
            ;
        }
    }
    img = restore < 0 ? NULL : fopen(argv[optind + 2], restore ? "rb" : "w+b");
    if (!img && restore >= 0) {
        perror(argv[optind + 2]);
        return 1;
    }
    if (restore > 0) {
        fseek(img, 0, SEEK_END);
        x.size = (uint32_t)ftell(img);
        x.type = ximg_type_from_size(x.size);
        if (!x.type) {
            fprintf(stderr, "unknown image size %lu\n", (unsigned long)x.size);
            return 1;
        }
    }
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    sl = simlink_open(argv[optind], SIMLINK_BUS_IEEE, 0, 0xff);
    if (!sl) {
        sl = simlink_open(argv[optind], SIMLINK_BUS_IEEE, 1, 0xff);
    }
    if (!sl) {
        perror("simlink_open");
        return 1;
    }
    slot = simlink_attach(sl, SL_TYPE_TOOL, 0);
    if (slot < 0) {
        fprintf(stderr, "no free slot\n");
        return 1;
    }
    for (c = 0; simlink_others(sl, slot) == 0 && !stop; c++) {
        usleep(10000);
        if (c > 3000) {
            fprintf(stderr, "no other participant after 30 s\n");
            quit(1);
        }
    }
    off = simlink_start_time(sl, slot);
    t = 0;
    rd();
    if (getenv("XIMG_REN")) {
        extra_pull = SL_IEEE_REN;
        set(0, 0);
    }
    wait_us((uint64_t)(boot_s * 1e6));     /* drive powers up */

    t0 = t;
    if (restore == -3 || restore == -4) {
        quit(track_test(x.dev, argv[optind + 2], restore == -4));
    }
    if (restore == -2) {
        quit(fast_test(x.dev, argv[optind + 2]));
    }
    if (restore < 0) {
        /* diagnosis: commands separated by ';', '?' = read status 10x every
           20 ms, '<n' = read n bytes from channel 2 */
        char *p = argv[optind + 2], *q;
        uint8_t b[300];
        verbose = 2;
        for (; p && *p; p = q) {
            q = strchr(p, ';');
            if (q) {
                *q++ = 0;
            }
            if (*p == '?') {
                for (c = 0; c < 10; c++) {
                    hal_recv(NULL, x.dev, 15, b, 41, 20000);
                    wait_us(20000);
                }
            } else if (*p == '<') {
                r = hal_recv(NULL, x.dev, 2, b, (uint16_t)atoi(p + 1), 20000);
                fprintf(stderr, "%10.3f ms channel 2: %d bytes:", t / 1e9, r);
                for (c = 0; c < r; c++) {
                    fprintf(stderr, " %02x", b[c]);
                }
                fputc('\n', stderr);
            } else if (*p == 'r') {
                /* r<hex>: M-R 2 bytes */
                unsigned ad = (unsigned)strtoul(p + 1, NULL, 16);
                char mr[6] = { 'M', '-', 'R', (char)(ad & 255), (char)(ad >> 8), 2 };
                int k;
                hal_send(NULL, x.dev, 0x6f, (const uint8_t *)mr, 6, 1);
                wait_us(getenv("XIMG_MRWAIT") ? (uint64_t)atoi(getenv("XIMG_MRWAIT")) : 0);
                k = hal_recv(NULL, x.dev, 15, b, 2, 2000);
                fprintf(stderr, "M-R $%04X: %02x %02x (%d)\n", ad, b[0], b[1], k);
            } else if (*p == 'o') {
                hal_send(NULL, x.dev, 0xff, (const uint8_t *)"I0", 2, 1);
            } else if (*p == 'w') {
                wait_us((uint64_t)atoi(p + 1) * 1000);
            } else if (*p == '!') {
                hal_recv(NULL, x.dev, 15, b, 41, 20000);
            } else if (*p == '#') {
                /* #ch=n : open channel ch with buffer n */
                int ch = atoi(p + 1);
                char *e = strchr(p, '=');
                char nm[4] = "#";
                if (e) {
                    strncpy(nm + 1, e + 1, 2);
                }
                hal_send(NULL, x.dev, (uint8_t)(0xf0 | (ch ? ch : 2)), (const uint8_t *)nm, (uint16_t)strlen(nm), 1);
            } else {
                hal_send(NULL, x.dev, 0x6f, (const uint8_t *)p, (uint16_t)strlen(p), 1);
            }
        }
        quit(0);
    }
    r = restore ? ximg_restore(&x, &h) : ximg_backup(&x, &h);
    fprintf(stderr, "%s: result %d, disk \"%s\",%s D%u, DOS %u, %s mode, %u blocks, %u errors, last status %s\n",
            restore ? "restore" : "backup", r, x.name, x.id, x.type, x.dos, x.fast ? "track" : "block",
            x.blocks, x.errors, x.msg);
    fprintf(stderr, "time %.3f s (bus time), %lu bus transactions, %lu bytes out, %lu bytes in\n",
            (t - t0) / 1e12, n_cmd, n_bytes_out, n_bytes_in);
    if (!restore) {
        fflush(img);
        if (ftruncate(fileno(img), (off_t)x.size)) {
            perror("truncate");
        }
    }
    fclose(img);
    quit(r == XIMG_OK ? 0 : 3);
    return 0;
}
