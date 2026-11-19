/* sdmodel.c - SD-Karte im SPI-Modus (aus petsdsim), fuer psd2sim */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static int verbose = 0;
static FILE *img;
static uint64_t img_blocks;
static uint8_t cmd[6];
static int cmdlen = 0;
static int acmd = 0;
static uint8_t out[1200];        /* Antwortbytes */
static int outlen = 0, outpos = 0;
enum { SD_CMD, SD_WRWAIT, SD_WRDATA, SD_MWRWAIT, SD_MWRDATA, SD_RDMULTI } sdst = SD_CMD;
static uint8_t wbuf[514];
static int wpos = 0;
static uint32_t waddr = 0, raddr = 0;
static int sd_ss = 1;
static int idle = 1;
static unsigned long spi_n = 0;

static uint16_t crc16(const uint8_t *p, int n)
{
    uint16_t c = 0;
    int i, k;
    for (i = 0; i < n; i++) {
        c ^= (uint16_t)p[i] << 8;
        for (k = 0; k < 8; k++) {
            c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
        }
    }
    return c;
}

static void q(uint8_t b) { if (outlen < (int)sizeof out) out[outlen++] = b; }

static void q_block(uint32_t blk)
{
    uint8_t d[512];
    uint16_t c;
    memset(d, 0, sizeof d);
    if (blk < img_blocks) {
        fseek(img, (long)blk * 512, SEEK_SET);
        if (fread(d, 1, 512, img) != 512) {
            memset(d, 0, sizeof d);
        }
    }
    c = crc16(d, 512);
    q(0xff); q(0xfe);
    if (outlen + 514 > (int)sizeof out) {
        return;
    }
    memcpy(out + outlen, d, 512);
    outlen += 512;
    q((uint8_t)(c >> 8)); q((uint8_t)c);
}

static void sd_command(void)
{
    int c = cmd[0] & 0x3f;
    uint32_t arg = ((uint32_t)cmd[1] << 24) | ((uint32_t)cmd[2] << 16) | ((uint32_t)cmd[3] << 8) | cmd[4];
    uint8_t r1 = idle ? 0x01 : 0x00;
    outlen = outpos = 0;
    q(0xff);                    /* Ncr */
    if (verbose > 2) {
        fprintf(stderr, "psd2sim: SD %sCMD%d %08x\n", acmd ? "A" : "", c, arg);
    }
    if (acmd) {
        acmd = 0;
        if (c == 41) {
            idle = 0;
            q(0x00);
        } else {
            q(r1);
        }
        return;
    }
    switch (c) {
    case 0:  idle = 1; q(0x01); break;
    case 1:  idle = 0; q(0x00); break;          /* MMC-Init, SD ignoriert */
    case 8:  q(0x01); q(0); q(0); q(0x01); q(cmd[4]); break;
    case 55: q(r1); acmd = 1; break;
    case 58: q(r1); q(0xc0); q(0xff); q(0x80); q(0x00); break;   /* CCS = 1 */
    case 59: case 16: q(r1); break;
    case 9: {                   /* CSD v2 */
        uint8_t csd[16] = { 0x40, 0x0e, 0x00, 0x32, 0x5b, 0x59, 0x00, 0, 0, 0,
                            0x7f, 0x80, 0x0a, 0x40, 0x00, 0x01 };
        uint32_t cs = (uint32_t)(img_blocks / 1024) - 1;
        uint16_t cc;
        csd[7] = (uint8_t)((cs >> 16) & 0x3f);
        csd[8] = (uint8_t)(cs >> 8);
        csd[9] = (uint8_t)cs;
        cc = crc16(csd, 16);
        q(0x00); q(0xff); q(0xfe);
        memcpy(out + outlen, csd, 16); outlen += 16;
        q((uint8_t)(cc >> 8)); q((uint8_t)cc);
        break;
    }
    case 10: {                  /* CID */
        uint8_t cid[16] = { 0x03, 'S', 'D', 'S', 'I', 'M', 'L', 'K', 0x10, 1, 2, 3, 4, 0x01, 0x1a, 0x01 };
        uint16_t cc = crc16(cid, 16);
        q(0x00); q(0xff); q(0xfe);
        memcpy(out + outlen, cid, 16); outlen += 16;
        q((uint8_t)(cc >> 8)); q((uint8_t)cc);
        break;
    }
    case 13: q(0x00); q(0x00); break;
    case 17: q(0x00); q_block(arg); break;
    case 18: q(0x00); raddr = arg; q_block(raddr++); sdst = SD_RDMULTI; break;
    case 12: sdst = SD_CMD; q(0xff); q(0x00); q(0x00); q(0xff); break;
    case 24: q(0x00); waddr = arg; sdst = SD_WRWAIT; break;
    case 25: q(0x00); waddr = arg; sdst = SD_MWRWAIT; break;
    default:
        if (verbose) {
            fprintf(stderr, "psd2sim: SD unbekannter CMD%d\n", c);
        }
        q(0x04);                /* illegal command */
        break;
    }
}

static uint8_t sd_byte(uint8_t in)
{
    uint8_t o = 0xff;
    if (sd_ss) {
        return 0xff;
    }
    if (outpos < outlen) {
        o = out[outpos++];
    } else if (sdst == SD_RDMULTI) {
        outlen = outpos = 0;
        q_block(raddr++);
        o = out[outpos++];
    }
    switch (sdst) {
    case SD_CMD:
    case SD_RDMULTI:
        if (cmdlen == 0 && (in & 0xc0) != 0x40) {
            break;
        }
        cmd[cmdlen++] = in;
        if (cmdlen == 6) {
            cmdlen = 0;
            if (sdst == SD_RDMULTI && (cmd[0] & 0x3f) != 12) {
                break;
            }
            sd_command();
        }
        break;
    case SD_WRWAIT:
    case SD_MWRWAIT:
        if (in == 0xfe || in == 0xfc) {
            wpos = 0;
            sdst = sdst == SD_WRWAIT ? SD_WRDATA : SD_MWRDATA;
        } else if (in == 0xfd && sdst == SD_MWRWAIT) {
            outlen = outpos = 0;
            q(0xff); q(0x00); q(0x00); q(0xff);
            sdst = SD_CMD;
        } else if ((in & 0xc0) == 0x40 && sdst == SD_MWRWAIT) {
            sdst = SD_CMD;
            cmd[0] = in;
            cmdlen = 1;
        }
        break;
    case SD_WRDATA:
    case SD_MWRDATA:
        wbuf[wpos++] = in;
        if (wpos == 514) {
            if (waddr < img_blocks) {
                fseek(img, (long)waddr * 512, SEEK_SET);
                fwrite(wbuf, 1, 512, img);
                fflush(img);
            }
            waddr++;
            outlen = outpos = 0;
            q(0x05); q(0x00); q(0x00); q(0xff);
            sdst = sdst == SD_WRDATA ? SD_CMD : SD_MWRWAIT;
        }
        break;
    }
    return o;
}

int sd_open(const char *path, int v)
{
    verbose = v;
    img = fopen(path, "r+b");
    if (!img) return -1;
    fseek(img, 0, SEEK_END);
    img_blocks = (uint64_t)ftell(img) / 512;
    return 0;
}

unsigned long sd_blocks(void) { return (unsigned long)img_blocks; }

void sd_select(int ss)
{
    sd_ss = ss ? 1 : 0;
    if (sd_ss) {
        cmdlen = 0;
        outlen = outpos = 0;
    }
}

uint8_t sd_xfer(uint8_t in) { spi_n++; return sd_byte(in); }
