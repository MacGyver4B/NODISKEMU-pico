/* blkdev.c - SD-Karte als Datei oder Laufwerk (512-Byte-Bloecke), siehe blkdev.h
 *
 * Offsets sind 64 Bit (stdio mit long war unter Windows auf 2 GB begrenzt).
 *
 * Schreibpuffer: Geschriebene Bloecke sammeln sich im Speicher und gehen
 * gebuendelt (aufsteigend sortiert, zusammenhaengende Laeufe in einem
 * Zugriff) hinaus, sobald 0,3 s Echtzeit lang nichts mehr geschrieben
 * wurde, der Puffer voll ist oder beim Schliessen. Ein USB-Stick ist bei
 * einzelnen 512-Byte-Schreibzugriffen sehr langsam (er schreibt intern
 * jedesmal einen ganzen Loeschblock); FAT und Verzeichnis werden ausserdem
 * mehrfach geschrieben und gehen so nur einmal hinaus. Gelesen wird aus dem
 * Puffer, wenn der Block dort liegt.
 *
 * Vorauslesen: Ein Block, der nicht im Puffer liegt, wird zusammen mit den
 * folgenden 63 gelesen (32 KB in einem Zugriff). Unter Windows puffert das
 * Betriebssystem beim direkten Laufwerkszugriff nichts; die FAT-Suche nach
 * freien Clustern liest sonst Tausende Sektoren einzeln ueber USB.
 *
 * Ein gezogener Stick gilt erst als entfernt, wenn er dreimal in Folge
 * (1,5 s) weder die Zustandsabfrage beantwortet noch Sektor 0 liefert;
 * einzelne Lese-/Schreibfehler werden nur gemeldet.
 *
 * Unter Windows verlangt ein Laufwerk (\\.\PhysicalDriveN) Zugriffe in
 * ganzen Sektoren aus ausgerichteten Puffern. Schreiben auf ein Laufwerk
 * gelingt nur, wenn Windows das Volume nicht mehr benutzt - das sperrt
 * petsd-sim.py vor dem Start.
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "blkdev.h"

#define RUN_MAX     2048            /* Bloecke je Schreibzugriff (1 MB) */
#define CACHE_SLOTS 8192            /* Hash-Plaetze (Zweierpotenz) */
#define CACHE_MAX   4096            /* gepufferte Bloecke (2 MB), dann leeren */
#define IDLE_MS     300             /* so lange Ruhe, dann leeren */
#define RA_BLOCKS   64              /* Vorauslesen: 32 KB je Lesezugriff */

static const char *prefix = "blkdev";
static int told_r, told_w;
static int is_dev;          /* Laufwerk statt Datei */
static int gone;            /* Laufwerk entfernt (endgueltig) */

static uint8_t rbuf[RUN_MAX * 512] __attribute__((aligned(4096)));

static uint8_t ra_buf[RA_BLOCKS * 512] __attribute__((aligned(4096)));
static uint64_t ra_start, ra_n;     /* Bloecke im Vorlesepuffer */
static uint64_t total_blocks;

static uint64_t c_blk[CACHE_SLOTS];
static uint8_t c_used[CACHE_SLOTS];
static uint8_t *c_data;
static int c_count;
static uint64_t last_write_ms;

void blk_set_prefix(const char *p) { prefix = p; }

static void set_gone(void)
{
    if (!gone) {
        gone = 1;
        fprintf(stderr, "%s: Karte entfernt (Laufwerk antwortet nicht mehr) - "
                "zum Wiedereinbinden die Simulation neu starten\n", prefix);
        if (c_count) {
            fprintf(stderr, "%s: %d gepufferte Bloecke nicht mehr geschrieben\n", prefix, c_count);
        }
        memset(c_used, 0, sizeof c_used);
        c_count = 0;
    }
}

#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>

static HANDLE h = INVALID_HANDLE_VALUE;

static uint64_t now_ms(void) { return GetTickCount64(); }

static void werr(const char *what, DWORD e)
{
    char msg[256];
    msg[0] = 0;
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e, 0,
                   msg, sizeof msg, NULL);
    msg[strcspn(msg, "\r\n")] = 0;
    fprintf(stderr, "%s: %s: Fehler %lu (%s)\n", prefix, what, (unsigned long)e, msg);
}

static int sys_open(const char *path, uint64_t *blocks)
{
    LARGE_INTEGER sz;
    GET_LENGTH_INFORMATION li;
    DWORD n;

    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        werr(path, GetLastError());
        return -1;
    }
    if (GetFileSizeEx(h, &sz)) {
        *blocks = (uint64_t)sz.QuadPart / 512;
    } else if (DeviceIoControl(h, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &li, sizeof li, &n, NULL)) {
        *blocks = (uint64_t)li.Length.QuadPart / 512;
        is_dev = 1;
    } else {
        werr(path, GetLastError());
        CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
        return -1;
    }
    return 0;
}

/* buf muss auf 4096 ausgerichtet sein (rbuf) */
static int raw_rw(uint64_t blk, uint8_t *buf, unsigned n, int wr)
{
    OVERLAPPED ov;
    DWORD got = 0, len = n * 512;
    BOOL ok;

    memset(&ov, 0, sizeof ov);
    ov.Offset = (DWORD)(blk * 512);
    ov.OffsetHigh = (DWORD)((blk * 512) >> 32);
    ok = wr ? WriteFile(h, buf, len, &got, &ov) : ReadFile(h, buf, len, &got, &ov);
    if (!ok || got != len) {
        DWORD e = GetLastError();
        int *told = wr ? &told_w : &told_r;
        if (!*told) {
            char what[64];
            snprintf(what, sizeof what, "%s Block %llu", wr ? "Schreiben" : "Lesen",
                     (unsigned long long)blk);
            werr(what, e);
            *told = 1;
        }
        return -1;
    }
    return 0;
}

static int dev_check(void)
{
    DWORD n;
    return DeviceIoControl(h, IOCTL_STORAGE_CHECK_VERIFY2, NULL, 0, NULL, 0, &n, NULL) ? 0 : -1;
}

static void sys_close(void)
{
    if (h != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(h);
        CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
    }
}

#else  /* POSIX */
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef __linux__
#include <sys/ioctl.h>
#include <linux/fs.h>
#endif

static int fd = -1;

static uint64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}

static int sys_open(const char *path, uint64_t *blocks)
{
    off_t end;
    struct stat st;

    fd = open(path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "%s: %s: %s\n", prefix, path, strerror(errno));
        return -1;
    }
    end = lseek(fd, 0, SEEK_END);     /* auch fuer Blockgeraete unter Linux */
    if (end <= 0) {
        fprintf(stderr, "%s: %s: Groesse nicht ermittelbar\n", prefix, path);
        close(fd);
        fd = -1;
        return -1;
    }
    *blocks = (uint64_t)end / 512;
    is_dev = fstat(fd, &st) == 0 && S_ISBLK(st.st_mode);
    return 0;
}

static int raw_rw(uint64_t blk, uint8_t *buf, unsigned n, int wr)
{
    size_t len = (size_t)n * 512;
    ssize_t r = wr ? pwrite(fd, buf, len, (off_t)(blk * 512)) : pread(fd, buf, len, (off_t)(blk * 512));
    if (r != (ssize_t)len) {
        int *told = wr ? &told_w : &told_r;
        if (!*told) {
            fprintf(stderr, "%s: %s Block %llu: %s\n", prefix, wr ? "Schreiben" : "Lesen",
                    (unsigned long long)blk, strerror(errno));
            *told = 1;
        }
        return -1;
    }
    return 0;
}

static int dev_check(void)
{
#ifdef __linux__
    uint64_t sz = 0;
    if (ioctl(fd, BLKGETSIZE64, &sz) != 0 || sz == 0) return -1;
#endif
    return 0;
}

static void sys_close(void)
{
    if (fd >= 0) {
        fsync(fd);
        close(fd);
        fd = -1;
    }
}
#endif

/* ----------------------------- Schreibpuffer ----------------------------- */

static int slot_of(uint64_t blk, int insert)
{
    unsigned i = (unsigned)((blk * 2654435761u) & (CACHE_SLOTS - 1));
    while (c_used[i]) {
        if (c_blk[i] == blk) return (int)i;
        i = (i + 1) & (CACHE_SLOTS - 1);
    }
    if (!insert) return -1;
    c_used[i] = 1;
    c_blk[i] = blk;
    c_count++;
    return (int)i;
}

static int cmp_slot(const void *a, const void *b)
{
    uint64_t x = c_blk[*(const int *)a], y = c_blk[*(const int *)b];
    return x < y ? -1 : x > y;
}

static void flush(void)
{
    static int idx[CACHE_SLOTS];
    int n = 0, i, j;

    if (!c_count) return;
    for (i = 0; i < CACHE_SLOTS; i++)
        if (c_used[i]) idx[n++] = i;
    qsort(idx, (size_t)n, sizeof idx[0], cmp_slot);
    for (i = 0; i < n && !gone; i = j) {
        uint64_t start = c_blk[idx[i]];
        for (j = i; j < n && j - i < RUN_MAX && c_blk[idx[j]] == start + (uint64_t)(j - i); j++)
            memcpy(rbuf + (size_t)(j - i) * 512, c_data + (size_t)idx[j] * 512, 512);
        raw_rw(start, rbuf, (unsigned)(j - i), 1);
    }
    memset(c_used, 0, sizeof c_used);
    c_count = 0;
    /* Der Vorlesepuffer kann Bloecke enthalten, die beim Vorlesen noch
       ungeschrieben im Schreibpuffer lagen (also mit altem Inhalt von der
       Karte gelesen wurden). Bisher deckte der Schreibpuffer sie ab; nach dem
       Leeren wuerde der alte Inhalt sichtbar. Daher verwerfen. */
    ra_n = 0;
}

/* -------------------------------- Schnittstelle -------------------------------- */

int blk_open(const char *path, uint64_t *blocks)
{
    c_data = malloc((size_t)CACHE_SLOTS * 512);
    if (!c_data) {
        fprintf(stderr, "%s: kein Speicher fuer den Schreibpuffer\n", prefix);
        return -1;
    }
    if (sys_open(path, blocks)) return -1;
    total_blocks = *blocks;
    return 0;
}

int blk_read(uint64_t blk, uint8_t *d)
{
    int s;
    if (gone) return -1;
    if ((s = slot_of(blk, 0)) >= 0) {
        memcpy(d, c_data + (size_t)s * 512, 512);
        return 0;
    }
    if (blk >= ra_start && blk < ra_start + ra_n) {
        memcpy(d, ra_buf + (size_t)(blk - ra_start) * 512, 512);
        return 0;
    }
    {
        uint64_t n = total_blocks > blk ? total_blocks - blk : 1;
        if (n > RA_BLOCKS) n = RA_BLOCKS;
        ra_n = 0;
        if (raw_rw(blk, ra_buf, (unsigned)n, 0)) {
            if (n == 1 || raw_rw(blk, ra_buf, 1, 0)) return -1;   /* Ende/Fehler: einzeln */
            n = 1;
        }
        ra_start = blk;
        ra_n = n;
    }
    memcpy(d, ra_buf, 512);
    return 0;
}

int blk_write(uint64_t blk, const uint8_t *d)
{
    int s;
    if (gone) return -1;
    if (c_count >= CACHE_MAX && slot_of(blk, 0) < 0) flush();
    s = slot_of(blk, 1);
    memcpy(c_data + (size_t)s * 512, d, 512);
    if (blk >= ra_start && blk < ra_start + ra_n)        /* Vorlesepuffer aktuell halten */
        memcpy(ra_buf + (size_t)(blk - ra_start) * 512, d, 512);
    last_write_ms = now_ms();
    return 0;
}

int blk_present(void)
{
    static uint64_t next_check;
    uint64_t t;

    if (gone) return 0;
    if (!c_count && !is_dev) return 1;
    t = now_ms();
    if (c_count && t - last_write_ms >= IDLE_MS) flush();
    if (is_dev && t >= next_check) {
        /* erst nach drei Fehlschlaegen in Folge (1,5 s) als gezogen werten:
           ein ausgelasteter Stick antwortet auch mal nicht */
        static int fails;
        next_check = t + 500;
        if (dev_check() == 0) {
            fails = 0;
        } else {
            /* Abfrage gescheitert (Windows beantwortet sie bei einem
               ausgelasteten Stick nicht immer): mit einem echten Lesezugriff
               gegenpruefen - ein vorhandener Stick liefert den Sektor */
            static uint8_t probe[512] __attribute__((aligned(4096)));
            fails = raw_rw(0, probe, 1, 0) ? fails + 1 : 0;
        }
        if (fails >= 3) set_gone();
    }
    return !gone;
}

void blk_close(void)
{
    flush();
    sys_close();
}
