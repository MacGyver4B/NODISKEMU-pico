/* blkdev.c - SD-Karte als Datei oder Laufwerk (512-Byte-Bloecke), siehe blkdev.h
 *
 * Offsets sind 64 Bit (stdio mit long war unter Windows auf 2 GB begrenzt).
 * Unter Windows verlangt ein Laufwerk (\\.\PhysicalDriveN) Zugriffe in
 * ganzen Sektoren; die Daten laufen deshalb ueber einen ausgerichteten
 * Puffer. Schreiben auf ein Laufwerk gelingt nur, wenn Windows das Volume
 * nicht mehr benutzt - das sperrt petsd-sim.py vor dem Start.
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#endif
#include <stdio.h>
#include <string.h>
#include "blkdev.h"

static const char *prefix = "blkdev";
static int told_r, told_w;

void blk_set_prefix(const char *p) { prefix = p; }

#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>

static HANDLE h = INVALID_HANDLE_VALUE;
static uint8_t abuf[4096] __attribute__((aligned(4096)));

static void werr(const char *what)
{
    char msg[256];
    DWORD e = GetLastError();
    msg[0] = 0;
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e, 0,
                   msg, sizeof msg, NULL);
    msg[strcspn(msg, "\r\n")] = 0;
    fprintf(stderr, "%s: %s: Fehler %lu (%s)\n", prefix, what, (unsigned long)e, msg);
}

int blk_open(const char *path, uint64_t *blocks)
{
    LARGE_INTEGER sz;
    GET_LENGTH_INFORMATION li;
    DWORD n;

    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        werr(path);
        return -1;
    }
    if (GetFileSizeEx(h, &sz)) {
        *blocks = (uint64_t)sz.QuadPart / 512;
    } else if (DeviceIoControl(h, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &li, sizeof li, &n, NULL)) {
        *blocks = (uint64_t)li.Length.QuadPart / 512;
    } else {
        werr(path);
        CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
        return -1;
    }
    return 0;
}

static int xfer(uint64_t blk, int wr)
{
    OVERLAPPED ov;
    DWORD n = 0;
    BOOL ok;

    memset(&ov, 0, sizeof ov);
    ov.Offset = (DWORD)(blk * 512);
    ov.OffsetHigh = (DWORD)((blk * 512) >> 32);
    ok = wr ? WriteFile(h, abuf, 512, &n, &ov) : ReadFile(h, abuf, 512, &n, &ov);
    if (!ok || n != 512) {
        int *told = wr ? &told_w : &told_r;
        if (!*told) {
            char what[64];
            snprintf(what, sizeof what, "%s Block %llu", wr ? "Schreiben" : "Lesen",
                     (unsigned long long)blk);
            werr(what);
            *told = 1;
        }
        return -1;
    }
    return 0;
}

int blk_read(uint64_t blk, uint8_t *d)
{
    if (xfer(blk, 0)) return -1;
    memcpy(d, abuf, 512);
    return 0;
}

int blk_write(uint64_t blk, const uint8_t *d)
{
    memcpy(abuf, d, 512);
    return xfer(blk, 1);
}

void blk_close(void)
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
#include <unistd.h>

static int fd = -1;

int blk_open(const char *path, uint64_t *blocks)
{
    off_t end;

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
    return 0;
}

int blk_read(uint64_t blk, uint8_t *d)
{
    if (pread(fd, d, 512, (off_t)(blk * 512)) != 512) {
        if (!told_r) {
            fprintf(stderr, "%s: Lesen Block %llu: %s\n", prefix, (unsigned long long)blk, strerror(errno));
            told_r = 1;
        }
        return -1;
    }
    return 0;
}

int blk_write(uint64_t blk, const uint8_t *d)
{
    if (pwrite(fd, d, 512, (off_t)(blk * 512)) != 512) {
        if (!told_w) {
            fprintf(stderr, "%s: Schreiben Block %llu: %s\n", prefix, (unsigned long long)blk, strerror(errno));
            told_w = 1;
        }
        return -1;
    }
    return 0;
}

void blk_close(void)
{
    if (fd >= 0) {
        fsync(fd);
        close(fd);
        fd = -1;
    }
}
#endif
