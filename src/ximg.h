/* NODISKEMU - disk imager in controller mode (X-IMG)
 *
 * Copies a whole disk between a real CBM drive on the IEEE-488 bus and a
 * disk image (D64/D80/D82), in both directions. The module knows nothing
 * about the hardware or the file system: both are reached through
 * ximg_hal_t, so the same code runs in the host test program
 * (sim/ximgtest, simlink) and in the firmware (ieee.c in controller mode,
 * FatFs).
 *
 * Two ways to move the blocks:
 * - track mode (DOS 2.1: 4040, DOS 2.7: 8050/8250/SFD-1001): small drive
 *   code on the DOS processor (M-W/M-E) puts jobs for a whole track into
 *   the job queue and moves the blocks with a two-wire handshake. Restore
 *   checks every block with a checksum on the DOS processor (write verify
 *   without sending the data back). Needs the fast_* functions of the HAL.
 * - block mode (all other drives, e.g. 2031, or without fast_*): U1/U2 per
 *   block, verify by reading back.
 *
 * Memory: two 256-byte block buffers.
 */
#ifndef XIMG_H
#define XIMG_H

#include <stdint.h>

/* image types (number = file extension) */
#define XIMG_D64 64
#define XIMG_D80 80
#define XIMG_D82 82
#define XIMG_D60 60     /* D9060: 153 cylinders x 4 heads x 32 sectors, backup only */
#define XIMG_D90 90     /* D9090: 153 cylinders x 6 heads x 32 sectors, backup only */
#define XIMG_D67 67     /* placeholder: DOS 1 (2040/3040), not supported yet */
#define XIMG_D71 71     /* placeholder: 1571, not supported yet */

/* DOS families (x->dos) */
#define XIMG_DOS_OTHER 0    /* block mode only (2031, unknown) */
#define XIMG_DOS_21    21   /* 4040 */
#define XIMG_DOS_27    27   /* 8050, 8250, SFD-1001 */

/* results */
#define XIMG_OK         0
#define XIMG_E_NODRIVE  1   /* drive does not answer */
#define XIMG_E_DISK     2   /* source disk not readable / not a CBM disk */
#define XIMG_E_FORMAT   3   /* unknown or unsupported format */
#define XIMG_E_MISMATCH 4   /* image does not fit this drive */
#define XIMG_E_IMAGE    5   /* image file error */
#define XIMG_E_DRIVE    6   /* drive error (format, channel, ...) */
#define XIMG_E_BADDISK  7   /* surface test found bad blocks */
#define XIMG_E_ABORT    8   /* declined by the user */
#define XIMG_E_ERRORS   9   /* finished, but with read/write errors */

/* questions for the user (ask callback): return 1 = yes */
#define XIMG_Q_FORMAT_OTHER 1   /* target disk has another format: format? */
#define XIMG_Q_FORMAT       2   /* format target disk? */
#define XIMG_Q_VERIFY       3   /* verify? (block mode only) */
#define XIMG_Q_RETRY_FORMAT 4   /* errors: format, test, rewrite? */

/* progress phases */
#define XIMG_P_READ    1
#define XIMG_P_WRITE   2
#define XIMG_P_SURFACE 3
#define XIMG_P_FORMAT  4

typedef struct {
    void *ctx;
    /* LISTEN dev, secondary sa (0x60|ch data, 0xf0|ch open), n bytes (EOI on
       the last if eoi), UNLISTEN. 0 = ok */
    int (*send)(void *ctx, uint8_t dev, uint8_t sa, const uint8_t *d, uint16_t n, uint8_t eoi);
    /* TALK dev, secondary 0x60|ch, read until EOI or max, UNTALK.
       first_ms: how long the drive may take for the first byte.
       returns number of bytes, <0 = no talker */
    int (*recv)(void *ctx, uint8_t dev, uint8_t ch, uint8_t *d, uint16_t max, uint32_t first_ms);
    /* LISTEN dev, 0xe0|ch, UNLISTEN */
    void (*close)(void *ctx, uint8_t dev, uint8_t ch);
    /* image file, random access. 0 = ok */
    int (*img_read)(void *ctx, uint32_t off, uint8_t *d, uint16_t n);
    int (*img_write)(void *ctx, uint32_t off, const uint8_t *d, uint16_t n);
    /* optional (may be NULL) */
    int (*ask)(void *ctx, int question);
    void (*progress)(void *ctx, int phase, uint8_t track, uint8_t tracks, uint16_t errors);
    void (*note)(void *ctx, const char *msg);  /* text for the user (status, errors) */
    /* optional fast transfer after M-E of the drive code (NULL: block mode).
       Per byte: the sender puts the byte on DIO and toggles DAV, the
       receiver acks by toggling NRFD. Data on the bus is inverted against
       the normal IEEE sense (raw port value of the drive).
       fast_begin: DAV/NRFD released, expected states reset.
       fast_send/fast_recv: return the number of bytes moved.
       fast_end: wait >= 200 us before the next ATN, so the drive sees the
       last ack (with ATN the drive hardware pulls NRFD/NDAC itself). */
    void (*fast_begin)(void *ctx);
    int (*fast_send)(void *ctx, const uint8_t *d, uint16_t n, uint32_t first_ms);
    int (*fast_recv)(void *ctx, uint8_t *d, uint16_t n, uint32_t first_ms);
    void (*fast_end)(void *ctx);
} ximg_hal_t;

typedef struct {
    uint8_t  dev, drive;        /* CBM drive: unit, drive 0/1 */
    uint8_t  type;              /* XIMG_D64/80/82 */
    uint8_t  dos;               /* XIMG_DOS_* (detected) */
    uint8_t  fast;              /* track mode in use */
    uint8_t  tracks;
    uint16_t blocks;
    uint16_t errors;            /* bad blocks (read errors / write errors) */
    uint32_t size;              /* image size in bytes (backup: incl. error table) */
    uint32_t etab;              /* backup: offset of the D64 error table, 0 = none yet */
    char     name[17];          /* disk name (from header) */
    char     id[3];
    uint8_t  status;            /* last drive status number */
    char     msg[42];           /* last drive status text */
    /* options; set before ximg_backup/ximg_restore */
    uint8_t  opt_format;        /* restore: 0 = ask, 1 = always, 2 = never */
    uint8_t  opt_verify;        /* restore, block mode: 0 = ask, 1 = yes, 2 = no */
    uint8_t  opt_slow;          /* 1 = block mode even if track mode is possible */
    uint8_t  buf[256];          /* block buffer */
    uint8_t  vbuf[256];         /* verify buffer */
} ximg_t;

/* number of sectors on track t for an image type */
uint8_t ximg_sectors(uint8_t type, uint8_t t);
/* image offset of block t/s */
uint32_t ximg_offset(uint8_t type, uint8_t t, uint8_t s);
/* type from image size (0 = unknown) */
uint8_t ximg_type_from_size(uint32_t size);
/* checksum of a block as the drive code computes it (2 bytes) */
void ximg_checksum(const uint8_t *d, uint8_t out[2]);

/* disk -> image. Detects the format, fills x->type/name/id/size, writes the
   image through img_write (with D64 error table if there were errors). */
int ximg_backup(ximg_t *x, const ximg_hal_t *h);

/* image -> disk. x->type and x->size must be set (ximg_type_from_size). */
int ximg_restore(ximg_t *x, const ximg_hal_t *h);

#endif
