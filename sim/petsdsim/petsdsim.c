/*
 * petsdsim.c - petSD+ (NODISKEMU, ATmega1284P) in simavr am simlink-IEEE-Bus
 *
 *   petsdsim [-v] [-l lookahead_us] firmware.elf sdkarte.img bus
 *
 * Die Firmware laeuft unveraendert in simavr (16 MHz, IEC 8 MHz, per CLKPR). Die IEEE-Pins des AVR
 * gehen ueber ein Modell der Treiber 75160/75161 auf den simlink-Bus, die
 * SD-Karte am SPI ist eine Abbilddatei (SDHC, Blockadressen). Mit
 * -DPSDPANEL (gelinkt gegen libpsdpanel.a aus NODISKEMU-pico sim/psdpanel)
 * kommt das Bedienfeld dazu: LCD, Tasten, ATtiny25 und DS3231 als dasselbe
 * Modell wie in psd2sim, Webseite mit -w port; sonst fehlen LCD, RTC und
 * I2C-Baustein und die Tasten (ADC7) stehen auf "keine Taste".
 *
 * EEPROM des ATmega1284P (4 KB) in einer Datei, Vorgabe PETSD.EEP im
 * aktuellen Verzeichnis (-e datei, -e - = nicht speichern): beim Start
 * geladen, bei Aenderung jede simulierte Sekunde und am Ende geschrieben.
 * Ohne Datei gilt der Inhalt aus dem ELF (.eeprom), wie frisch geflasht.
 *
 * Bus "-": ohne simlink allein laufen (nur Bedienfeld/SD-Karte), Ende nach
 * -t ms simulierter Zeit.
 *
 * Zeitkopplung: konservativ wie die VICE-Busmodule, aber mit Vorausschau
 * L: Eingaenge vom Bus wirken L spaeter auf den AVR. Damit darf der AVR bis
 * Fortschritt der anderen + L rechnen, und beide Seiten kommen je Runde um
 * L voran (statt um 1 ps). Fuer den IEEE-Handshake ist eine Verzoegerung von
 * wenigen us unerheblich.
 *
 * Pinbelegung (NODISKEMU src/avr/arch-config.h, HW_PETSDPLUS):
 *   PA0-6, PD7  D1-D8       PC3 TE (1 = senden)
 *   PC7 NRFD   PC6 NDAC   PC5 DAV   PC4 EOI   PC2 IFC   PD2 ATN (INT0)
 *   PB4 SS, PB5 MOSI, PB6 MISO, PB7 SCK (SD)   PD5 Karte da (low)
 *   PD6 Schreibschutz (high = geschuetzt)   PA7/ADC7 Tasten
 *   LCD: PB0-3 DB4-DB7, PD3 RS, PD4 E
 *   I2C (softi2c): SCL PC0, SDA PC1; ATtiny25 0x4C, DS3231 0xD0
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sim_avr.h"
#include "sim_elf.h"
#include "sim_irq.h"
#include "avr_ioport.h"
#include "avr_spi.h"
#include "avr_adc.h"
#include "avr_eeprom.h"
#include "avr_uart.h"

#include "simlink.h"
#include "buscap.h"
#include "../blkdev.h"


/* Datenadressen der Ports (I/O + 0x20) */
#define A_PINA 0x20
#define A_DDRA 0x21
#define A_PORTA 0x22
#define A_PINC 0x26
#define A_DDRC 0x27
#define A_PORTC 0x28
#define A_PIND 0x29
#define A_DDRD 0x2a
#define A_PORTD 0x2b

static volatile sig_atomic_t stop = 0;
static int verbose = 0;
static avr_t *avr;
static unsigned min_sp = 0xffff;   /* tiefster Stapelzeiger */
static simlink_t *sl;
static int slot;
static uint64_t off;            /* Startzeit am Bus */
static uint64_t look = 2000000; /* Vorausschau in ps */

static void on_sig(int s) { (void)s; stop = 1; }

/* ---------------------------- Bedienfeld ---------------------------- */

#ifdef PSDPANEL
/* libpsdpanel.a (Rust, sim/psdpanel in NODISKEMU-pico) */
void    psdpanel_init(uint16_t port, const char *hold, uint32_t verbose);
void    psdpanel_lcd_pins(uint8_t e, uint8_t rs, uint8_t nib);
uint8_t psdpanel_i2c_start(uint8_t addr, uint8_t read);
uint8_t psdpanel_i2c_write(uint8_t byte);
uint8_t psdpanel_i2c_read(void);
void    psdpanel_i2c_stop(void);
uint8_t psdpanel_tick(double sim_ms);
uint32_t psdpanel_key_mv(uint8_t keys);
void    psdpanel_leds(uint8_t l);
#endif
/* ---------------------------- EEPROM-Datei ---------------------------- */

#define EE_SIZE 4096
static const char *ee_path = "PETSD.EEP";
static uint8_t ee_saved[EE_SIZE];
static uint64_t ee_next = 0;

static void ee_load(void)
{
    uint8_t buf[EE_SIZE];
    avr_eeprom_desc_t d;
    FILE *f;
    d.ee = ee_saved; d.offset = 0; d.size = EE_SIZE;
    avr_ioctl(avr, AVR_IOCTL_EEPROM_GET, &d);       /* Stand aus dem ELF */
    if (!ee_path || !(f = fopen(ee_path, "rb"))) {
        return;
    }
    if (fread(buf, 1, EE_SIZE, f) == EE_SIZE) {
        d.ee = buf;
        avr_ioctl(avr, AVR_IOCTL_EEPROM_SET, &d);
        memcpy(ee_saved, buf, EE_SIZE);
        fprintf(stderr, "petsdsim: EEPROM aus %s\n", ee_path);
    }
    fclose(f);
}

static void ee_save(void)
{
    uint8_t buf[EE_SIZE];
    avr_eeprom_desc_t d;
    FILE *f;
    if (!ee_path) {
        return;
    }
    d.ee = buf; d.offset = 0; d.size = EE_SIZE;
    avr_ioctl(avr, AVR_IOCTL_EEPROM_GET, &d);
    if (!memcmp(buf, ee_saved, EE_SIZE)) {
        return;
    }
    if ((f = fopen(ee_path, "wb"))) {
        fwrite(buf, 1, EE_SIZE, f);
        fclose(f);
        memcpy(ee_saved, buf, EE_SIZE);
        if (verbose) {
            fprintf(stderr, "petsdsim: EEPROM nach %s\n", ee_path);
        }
    }
}

/* Takt: Der petSD+ hat einen 16-MHz-Quarz, die Fuses (LFUSE 0xF7, CKDIV8
   aus) lassen ihn ab Reset ungeteilt laufen. Die Firmware teilt per CLKPR
   nur fuer IEC auf 8 MHz (bus.h, IEC_SLOW_IEEE_FAST). simavr bildet CLKPR
   nicht nach; daher hier mitfuehren und die Zeit stueckweise aufsummieren. */
#define F_XTAL 16000000ULL
static uint64_t clk_ps, clk_cyc, ps_cycle = 1000000000000ULL / F_XTAL;
static uint32_t cur_hz = F_XTAL;

static uint64_t sim_ps(void)
{
    return clk_ps + (avr->cycle - clk_cyc) * ps_cycle;
}

static void clkpr_write(struct avr_t *a, avr_io_addr_t addr, uint8_t v, void *param)
{
    (void)param;
    a->data[addr] = v;
    if (v & 0x80) {
        return;                         /* CLKPCE, Wert folgt */
    }
    clk_ps = sim_ps();
    clk_cyc = a->cycle;
    cur_hz = (uint32_t)(F_XTAL >> (v & 0x0f));
    ps_cycle = 1000000000000ULL / cur_hz;
    a->frequency = cur_hz;
    if (verbose) {
        fprintf(stderr, "petsdsim: CLKPR %02x -> %u Hz\n", v, cur_hz);
    }
}

/* jede simulierte Sekunde */
static void ee_poll(void)
{
    if (sim_ps() >= ee_next) {
        ee_next = sim_ps() + 1000000000000ULL;
        ee_save();
    }
}

static int panel_on = 0;
static int panel_port = 0;
static const char *panel_hold = NULL;
static uint8_t panel_keys = 0;
static uint64_t panel_next = 0;

#ifdef PSDPANEL
/* E (PD4): bei jeder Aenderung RS (PD3) und DB4-DB7 (PB0-3) mitgeben */
static void lcd_e(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)param;
    psdpanel_lcd_pins(value ? 1 : 0, (avr->data[0x2b] >> 3) & 1, avr->data[0x25] & 0x0f);
}

/* I2C wie softi2c.c des petSD+: SCL PC0, SDA PC1, offener Kollektor ueber
   DDR (DDR=1 und PORT=0 zieht auf low). Slave auf Pin-Ebene fuer
   ATtiny25 und DS3231 hinter psdpanel; SDA des Slaves als externer Pegel. */
enum { I2C_IDLE, I2C_RX, I2C_RXACK, I2C_TX, I2C_TXACK };
static int i2c_st = I2C_IDLE, i2c_nbit, i2c_addr_phase, i2c_after_addr, i2c_rw, i2c_sel, i2c_ack, i2c_nack;
static uint8_t i2c_sh, i2c_tx;
static int i2c_low = 0, i2c_low_pub = -1, i2c_pscl = 1, i2c_psda = 1;
static void pin(char port, int bit, int v);

static void i2c_step(void)
{
    uint8_t ddr = avr->data[A_DDRC], port = avr->data[A_PORTC];
    int scl = (ddr & 1) ? (port & 1) : 1;
    int sda = (ddr & 2) ? ((port >> 1) & 1) : 1;      /* Pegel des Masters */

    if (scl && i2c_pscl) {
        if (i2c_psda && !sda) {                        /* START (auch wiederholt) */
            i2c_st = I2C_RX; i2c_nbit = 0; i2c_sh = 0; i2c_addr_phase = 1; i2c_low = 0;
        } else if (!i2c_psda && sda) {                 /* STOP */
            if (i2c_sel) {
                psdpanel_i2c_stop();
            }
            i2c_sel = 0; i2c_st = I2C_IDLE; i2c_low = 0;
        }
    }
    if (!i2c_pscl && scl) {                            /* steigende Flanke: abtasten */
        if (i2c_st == I2C_RX) {
            i2c_sh = (uint8_t)((i2c_sh << 1) | sda);
            i2c_nbit++;
        } else if (i2c_st == I2C_TXACK) {
            i2c_nack = sda;
        }
    }
    if (i2c_pscl && !scl) {                            /* fallende Flanke: weiter */
        switch (i2c_st) {
        case I2C_RX:
            if (i2c_nbit == 8) {
                if (i2c_addr_phase) {
                    i2c_rw = i2c_sh & 1;
                    i2c_sel = psdpanel_i2c_start(i2c_sh >> 1, (uint8_t)i2c_rw);
                    i2c_ack = i2c_sel;
                    i2c_after_addr = 1;
                    i2c_addr_phase = 0;
                } else {
                    i2c_ack = i2c_sel && psdpanel_i2c_write(i2c_sh);
                    i2c_after_addr = 0;
                }
                i2c_low = i2c_ack;
                i2c_st = I2C_RXACK;
            }
            break;
        case I2C_RXACK:
            i2c_low = 0;
            if (!i2c_ack) {
                i2c_st = I2C_IDLE;
            } else if (i2c_rw && i2c_after_addr) {
                i2c_tx = psdpanel_i2c_read(); i2c_nbit = 0;
                i2c_low = !(i2c_tx & 0x80);
                i2c_st = I2C_TX;
            } else {
                i2c_st = I2C_RX; i2c_nbit = 0; i2c_sh = 0;
            }
            break;
        case I2C_TX:
            if (++i2c_nbit < 8) {
                i2c_low = !((i2c_tx << i2c_nbit) & 0x80);
            } else {
                i2c_low = 0;
                i2c_st = I2C_TXACK;
            }
            break;
        case I2C_TXACK:
            if (!i2c_nack) {
                i2c_tx = psdpanel_i2c_read(); i2c_nbit = 0;
                i2c_low = !(i2c_tx & 0x80);
                i2c_st = I2C_TX;
            } else {
                i2c_low = 0;
                i2c_st = I2C_IDLE;
            }
            break;
        }
    }
    i2c_pscl = scl;
    i2c_psda = sda;
    if (i2c_low != i2c_low_pub) {
        i2c_low_pub = i2c_low;
        pin('C', 1, !i2c_low);
    }
}

static void panel_attach(void)
{
    psdpanel_init((uint16_t)panel_port, panel_hold, verbose > 1);
    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('D'), 4), lcd_e, NULL);
    panel_keys = psdpanel_tick(0);
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_ADC_GETIRQ, ADC_IRQ_ADC7), psdpanel_key_mv(panel_keys));
}
#endif

/* nach jedem Befehl I2C, jede simulierte Millisekunde Tastenfolge,
   Protokoll, ADC7 */
static void panel_step(void)
{
#ifdef PSDPANEL
    uint8_t k;
    if (!panel_on) {
        return;
    }
    i2c_step();
    if (sim_ps() < panel_next) {
        return;
    }
    panel_next = sim_ps() + 1000000000ULL;
    k = psdpanel_tick(sim_ps() / 1e9);
    {
        /* LEDs des petSD+: BUSY PD1 (gruen), DIRTY PD0 (rot), aktiv high */
        uint8_t on = avr->data[0x2b] & avr->data[0x2a];
        psdpanel_leds((uint8_t)(((on >> 1) & 1) | ((on & 1) << 1)));
    }
    if (k != panel_keys) {
        panel_keys = k;
        avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_ADC_GETIRQ, ADC_IRQ_ADC7), psdpanel_key_mv(k));
    }
#endif
}

static uint64_t now_ps(void)
{
    return off + sim_ps();
}

/* ------------------------------ SD-Karte ------------------------------ */

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
        if (blk_read(blk, d)) {
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
        fprintf(stderr, "petsdsim: SD %sCMD%d %08x\n", acmd ? "A" : "", c, arg);
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
            fprintf(stderr, "petsdsim: SD unbekannter CMD%d\n", c);
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
                blk_write(waddr, wbuf);
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


/* simavr 1.6 (Debian/Ubuntu-Paket) setzt jedes SPI-Byte pauschal mit 100 us
   an ("should be speed dependent"). Ein SD-Sektor dauert damit ueber 50 ms
   statt rund 1 ms, und der PET laeuft beim DIRECTORY (ein TALK pro Byte) in
   seinen 64-ms-Timeout. Den Timer daher auf die echte Dauer bei eingestelltem
   SPI-Takt kuerzen, wie es neuere simavr-Versionen selbst tun. */
static avr_io_t *spi_io;

static void spi_fix(void)
{
    static const uint8_t div[4] = { 4, 16, 64, 128 };
    avr_cycle_timer_slot_p t;
    uint8_t spcr = avr->data[0x4c];
    avr_cycle_count_t want;
    if (!spi_io) return;
    want = div[spcr & 3];
    if ((spcr & 0x10) && (avr->data[0x4d] & 0x01)) want >>= 1;   /* MSTR + SPI2X */
    want <<= 3;
    for (t = avr->cycle_timers.timer; t; t = t->next) {
        if (t->param == spi_io) {
            if (t->when > avr->cycle + want) {
                avr_cycle_timer_t fn = t->timer;
                avr_cycle_timer_cancel(avr, fn, spi_io);
                avr_cycle_timer_register(avr, want, fn, spi_io);
            }
            break;
        }
    }
}

static void spi_out(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)param;
    uint8_t r;
    spi_n++;
    r = sd_byte((uint8_t)value);
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_SPI_GETIRQ(0), SPI_IRQ_INPUT), r);
}

static void cap_note(void);

static void ss_change(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)param;
    sd_ss = value ? 1 : 0;
    cap_note();
    if (sd_ss) {
        cmdlen = 0;
        outlen = outpos = 0;    /* Rest einer Antwort verfaellt */
    }
}

/* ------------------------------ IEEE-Bus ------------------------------ */

static uint8_t pub_pull = 0, pub_data = 0;
static int pub_te = 0;
static int cap_on = 0, isr_on = 0;
static unsigned isr_sp;
static uint8_t ext_pull, ext_data;
static int ext_valid;

/* Zustand fuer den VCD-Mitschnitt (-g) */
static void cap_note(void)
{
    if (cap_on) {
        buscap_state(now_ps(), ext_valid ? ext_pull : 0, ext_valid ? ext_data : 0, pub_pull, pub_data,
                     pub_te, sd_ss, isr_on);
    }
}

/* INT0 (ATN) laeuft: Einsprung an Vektor 1 (Adresse 4) bis zum reti */
static void isr_track(void)
{
    unsigned sp = avr->data[0x5d] | (avr->data[0x5e] << 8);
    if (avr->pc == 0x0004) {
        if (!isr_on) {
            isr_on = 1;
            isr_sp = sp + 2;
            cap_note();
        }
    } else if (isr_on && sp >= isr_sp) {
        isr_on = 0;
        cap_note();
    }
}
static int pub_valid = 0;

/* Eingang von aussen treiben. Als "extern" markiert, damit ein Pull-up der
   Firmware den Pegel nicht ueberschreibt (simavr nimmt sonst den Pull-up). */
static uint8_t ext_mask[4], ext_val[4];

static void pin(char port, int bit, int v)
{
    avr_ioport_external_t e;
    int i = port - 'A';
    ext_mask[i] |= (uint8_t)(1 << bit);
    if (v) {
        ext_val[i] |= (uint8_t)(1 << bit);
    } else {
        ext_val[i] &= (uint8_t)~(1 << bit);
    }
    e.name = (unsigned long)port;
    e.mask = ext_mask[i];
    e.value = ext_val[i];
    avr_ioctl(avr, AVR_IOCTL_IOPORT_SET_EXTERNAL(port), &e);
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ(port), bit), v ? 1 : 0);
}

/* simavr 1.6 schreibt beim Lesen von PINx fuer Ausgaenge den PORT-Pegel
   ins PIN-Register. Wird der Pin danach Eingang, bleibt dieser Pegel dort
   stehen, wenn der extern getriebene Wert gleich der (gefilterten) IRQ-
   Leitung ist. Daher bei jedem DDR-Schreiben die extern getriebenen Pegel
   der Eingaenge sofort eintragen; value ist der neue DDR-Wert.
   Fund und Abhilfe: Torsten (tuti), Forum classic-computing.de. */
static void ddr_write(struct avr_irq_t *irq, uint32_t value, void *param)
{
    int i = (int)(intptr_t)param;
    static const uint8_t pin_addr[4] = { A_PINA, 0, A_PINC, A_PIND };
    uint8_t m = ext_mask[i] & (uint8_t)~value;
    (void)irq;
    avr->data[pin_addr[i]] = (uint8_t)((avr->data[pin_addr[i]] & ~m) | (ext_val[i] & m));
}

/* Neuere simavr-Versionen (ab 1.7) schreiben beim Ausgeben den PORT-Wert
   ins PIN-Register. Schaltet die Firmware den Pin danach auf Eingang,
   bleibt dort der alte Ausgangspegel stehen: der externe Pegel wird nicht
   neu eingetragen, weil sich der Wert der IRQ-Leitung nicht aendert
   (gefilterter IRQ). Beim Lesen von PINx die extern getriebenen Eingaenge
   daher selbst eintragen. simavr ruft diesen Hook bei jedem Lesen auf und
   liefert danach den Inhalt von PINx. simavr 1.6 ignoriert die Aenderung
   fuer den laufenden Lesezugriff; dort hilft ddr_write (oben). */
static void pin_read(struct avr_irq_t *irq, uint32_t value, void *param)
{
    int i = (int)(intptr_t)param;
    static const uint8_t pin_addr[4] = { A_PINA, 0, A_PINC, A_PIND };
    static const uint8_t ddr_addr[4] = { A_DDRA, 0, A_DDRC, A_DDRD };
    uint8_t m = ext_mask[i] & (uint8_t)~avr->data[ddr_addr[i]];
    (void)irq;
    avr->data[pin_addr[i]] = (uint8_t)((value & ~m) | (ext_val[i] & m));
}


/* Diagnose (PSD_HANG=1): Byte angenommen (eigenes NRFD low, NDAC frei),
   DAV ist laengst wieder high, aber die Firmware zieht NDAC nicht -> PC und
   Ruecksprungadressen ausgeben (einmal je Haenger, nach 1 ms) */
static int getenv_hang;
static void hang_check(void)
{
    static uint64_t since; static int told;
    int cond = ext_valid && (pub_pull & SL_IEEE_NRFD) && !(pub_pull & SL_IEEE_NDAC)
               && !(ext_pull & SL_IEEE_DAV);
    uint64_t t = now_ps();
    if (!cond) { since = 0; told = 0; return; }
    if (!since) since = t;
    if (!told && t - since > 1000000000ULL) {
        unsigned sp = avr->data[0x5d] | (avr->data[0x5e] << 8), k;
        told = 1;
        fprintf(stderr, "petsdsim %10.3f ms: HAENGER PC %05x SP %04x ATN %s Stapel:",
                t / 1e9, avr->pc, sp, (ext_pull & SL_IEEE_ATN) ? "low" : "high");
        for (k = 1; k <= 24 && sp + k < 0x4100; k++)
            fprintf(stderr, " %02x", avr->data[sp + k]);
        fprintf(stderr, "\n");
    }
}

/* Bus der anderen (pull/data: gesetzt = low) auf die AVR-Eingaenge */

static void apply_ext(uint8_t l, uint8_t d)
{
    int i;
    uint8_t ch = ext_valid ? (uint8_t)(l ^ ext_pull) : 0xff;
    uint8_t cd = ext_valid ? (uint8_t)(d ^ ext_data) : 0xff;
    if (ch & SL_IEEE_ATN)  pin('D', 2, !(l & SL_IEEE_ATN));
    if (ch & SL_IEEE_IFC)  pin('C', 2, !(l & SL_IEEE_IFC));
    if (ch & SL_IEEE_NRFD) pin('C', 7, !(l & SL_IEEE_NRFD));
    if (ch & SL_IEEE_NDAC) pin('C', 6, !(l & SL_IEEE_NDAC));
    if (ch & SL_IEEE_DAV)  pin('C', 5, !(l & SL_IEEE_DAV));
    if (ch & SL_IEEE_EOI)  pin('C', 4, !(l & SL_IEEE_EOI));
    for (i = 0; i < 7; i++) {
        if (cd & (1 << i)) pin('A', i, !(d & (1 << i)));
    }
    if (cd & 0x80) pin('D', 7, !(d & 0x80));
    if (verbose > 2 && (ch || cd)) {
        fprintf(stderr, "petsdsim %10.3f ms: Bus -> AVR %02x/%02x\n", now_ps() / 1e9, l, d);
    }
    ext_pull = l;
    ext_data = d;
    ext_valid = 1;
    cap_note();
}

/* Ausgaenge des AVR ueber die 75160/75161 auf den Bus */
static int drives(uint8_t ddr, uint8_t port, int bit)
{
    return (ddr & (1 << bit)) && !(port & (1 << bit));
}

static void publish(void)
{
    uint8_t ddrc = avr->data[A_DDRC], portc = avr->data[A_PORTC];
    uint8_t ddra = avr->data[A_DDRA], porta = avr->data[A_PORTA];
    uint8_t ddrd = avr->data[A_DDRD], portd = avr->data[A_PORTD];
    int te = (portc & 0x08) && (ddrc & 0x08);
    uint8_t p = 0, d = 0;
    int i;
    if (te) {
        if (drives(ddrc, portc, 5)) p |= SL_IEEE_DAV;
        if (drives(ddrc, portc, 4)) p |= SL_IEEE_EOI;
        for (i = 0; i < 7; i++) {
            if (drives(ddra, porta, i)) d |= (uint8_t)(1 << i);
        }
        if (drives(ddrd, portd, 7)) d |= 0x80;
    } else {
        if (drives(ddrc, portc, 7)) p |= SL_IEEE_NRFD;
        if (drives(ddrc, portc, 6)) p |= SL_IEEE_NDAC;
    }
    if (te != pub_te) {
        pub_te = te;
        cap_note();
    }
    if (!pub_valid || p != pub_pull || d != pub_data) {
        pub_valid = 1;
        pub_pull = p;
        pub_data = d;
        cap_note();
        simlink_set(sl, slot, now_ps(), p, d);
        if (verbose > 1) {
            fprintf(stderr, "petsdsim %10.3f ms: AVR -> Bus %02x/%02x%s\n", now_ps() / 1e9, p, d,
                    te ? " (TE)" : "");
        }
    }
}

/* UART0 (Debugausgabe der Firmware, falls eingebaut) */
static void uart_out(struct avr_irq_t *irq, uint32_t value, void *param)
{
    static char line[256];
    static int n = 0;
    (void)irq; (void)param;
    if (value == '\n' || n == (int)sizeof line - 1) {
        line[n] = 0;
        fprintf(stderr, "petsdsim %10.3f ms: UART %s\n", now_ps() / 1e9, line);
        n = 0;
    } else if (value != '\r') {
        line[n++] = (char)value;
    }
}

/* ------------------------------ Hauptteil ----------------------------- */

static double cap_idle = 0;
static unsigned long warn_late, warn_peek, warn_full;
static const char *cap_dir;

int main(int argc, char **argv)
{
    elf_firmware_t fw;
    int c;
    uint64_t done = 0, last_ext = 0;
    uint64_t pend_t[64];
    uint8_t pend_l[64], pend_d[64];
    int pend_n = 0;
    double limit_ms = 0;

    while ((c = getopt(argc, argv, "vl:w:pk:t:e:g:G:")) != -1) {
        switch (c) {
        case 'v': verbose++; break;
        case 'l': look = (uint64_t)(atof(optarg) * 1e6); break;
        case 'w': panel_on = 1; panel_port = atoi(optarg); break;   /* Bedienfeld + Webseite */
        case 'p': panel_on = 1; break;                              /* Bedienfeld ohne Webseite */
        case 'k': panel_on = 1; panel_hold = optarg; break;         /* Tasten ab Start */
        case 't': limit_ms = atof(optarg); break;                   /* Ende nach ms */
        case 'g': cap_on = 1; cap_dir = optarg; break;     /* VCD-Mitschnitte */
        case 'G': cap_idle = atof(optarg); break;                 /* Ruhezeit ms */
        case 'e': ee_path = strcmp(optarg, "-") ? optarg : NULL; break;  /* EEPROM-Datei */
        default: goto usage;
        }
    }
    if (argc - optind != 3) {
usage:
        fprintf(stderr, "usage: petsdsim [-v] [-l vorausschau_us] [-w port | -p] [-k tasten] [-t ms] [-e eeprom|-]\n"
                        "                [-g vcd-verzeichnis [-G ruhezeit_ms]] firmware.elf sdkarte.img bus|-\n");
        return 1;
    }
    if (cap_on) {
        buscap_init(cap_dir, cap_idle);
    }
#ifndef PSDPANEL
    if (panel_on) {
        fprintf(stderr, "petsdsim: ohne Bedienfeld uebersetzt (-DPSDPANEL, libpsdpanel.a)\n");
        return 1;
    }
#endif
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    memset(&fw, 0, sizeof fw);
    if (elf_read_firmware(argv[optind], &fw)) {
        fprintf(stderr, "petsdsim: %s nicht lesbar\n", argv[optind]);
        return 1;
    }
    avr = avr_make_mcu_by_name("atmega1284p");
    if (!avr) {
        return 1;
    }
    avr_init(avr);
    avr->frequency = F_XTAL;
    avr->avcc = 5000;
    avr->aref = 5000;
    avr->vcc = 5000;
    avr_load_firmware(avr, &fw);
    ee_load();
    avr->log = verbose > 2 ? 3 : 1;

    getenv_hang = getenv("PSD_HANG") != NULL;
    blk_set_prefix("petsdsim");
    if (blk_open(argv[optind + 1], &img_blocks)) {
        return 1;
    }

    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_SPI_GETIRQ(0), SPI_IRQ_OUTPUT), spi_out, NULL);
    {
        avr_io_t *io;
        for (io = avr->io_port; io; io = io->next) {
            if (io->kind && !strcmp(io->kind, "spi")) { spi_io = io; break; }
        }
    }
    {
        uint32_t f = 0;
        avr_ioctl(avr, AVR_IOCTL_UART_GET_FLAGS('0'), &f);
        f &= ~AVR_UART_FLAG_STDIO;
        avr_ioctl(avr, AVR_IOCTL_UART_SET_FLAGS('0'), &f);
        avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_OUTPUT), uart_out, NULL);
    }
    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), 4), ss_change, NULL);
    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('A'), IOPORT_IRQ_REG_PIN), pin_read, (void *)(intptr_t)0);
    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('C'), IOPORT_IRQ_REG_PIN), pin_read, (void *)(intptr_t)2);
    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('D'), IOPORT_IRQ_REG_PIN), pin_read, (void *)(intptr_t)3);
    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('A'), IOPORT_IRQ_DIRECTION_ALL), ddr_write, (void *)(intptr_t)0);
    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('C'), IOPORT_IRQ_DIRECTION_ALL), ddr_write, (void *)(intptr_t)2);
    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('D'), IOPORT_IRQ_DIRECTION_ALL), ddr_write, (void *)(intptr_t)3);
    avr_register_io_write(avr, 0x61, clkpr_write, NULL);    /* CLKPR */
    /* feste Eingaenge: Karte da, kein Schreibschutz, I2C-Leitungen hoch,
       Tasten: ADC7 etwa 550 (Ruhewert ohne Taste, readbuttons.inc) */
    pin('D', 5, 0);
    pin('D', 6, 0);
    pin('C', 0, 1);
    pin('C', 1, 1);
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_ADC_GETIRQ, ADC_IRQ_ADC7), 2690);
#ifdef PSDPANEL
    if (panel_on) {
        panel_attach();
    }
#endif

    if (strcmp(argv[optind + 2], "-") == 0) {
        /* allein: kein Bus, nur Bedienfeld und SD-Karte */
        fprintf(stderr, "petsdsim: %s, Karte %s, ohne Bus\n", argv[optind], argv[optind + 1]);
        while (!stop) {
            int st = avr_run(avr);
            spi_fix();
            if (cap_on) {
                isr_track();
                buscap_poll(now_ps());
            }
            if (st == cpu_Done || st == cpu_Crashed) {
                fprintf(stderr, "petsdsim: AVR angehalten (%d) bei PC %04x\n", st, avr->pc);
                break;
            }
            panel_step();
            ee_poll();
            if (limit_ms > 0 && sim_ps() / 1e9 >= limit_ms) {
                break;
            }
        }
        if (cap_on) buscap_close();
        ee_save();
        blk_close();
        return 0;
    }

    sl = simlink_open(argv[optind + 2], SIMLINK_BUS_IEEE, 0, 0xff);
    if (!sl) {
        sl = simlink_open(argv[optind + 2], SIMLINK_BUS_IEEE, 1, 0xff);
    }
    if (!sl) {
        perror("simlink_open");
        return 1;
    }
    slot = simlink_attach(sl, SL_TYPE_AVR, 0);
    if (slot < 0) {
        fprintf(stderr, "petsdsim: kein freier Platz am Bus\n");
        return 1;
    }
    fprintf(stderr, "petsdsim: %s, Karte %s (%llu Bloecke), Bus %s\n", argv[optind], argv[optind + 1],
            (unsigned long long)img_blocks, argv[optind + 2]);
    {
        int waited = 0;
        while (simlink_others(sl, slot) == 0 && !stop) {
            usleep(10000);
            if (++waited > 3000) {
                fprintf(stderr, "petsdsim: nach 30 s keine Gegenseite, Ende\n");
                return 1;
            }
        }
    }
    off = simlink_start_time(sl, slot);
    done = off;
    last_ext = off;
    {
        uint8_t d0 = 0, l0 = simlink_peek_others(sl, slot, off, &d0);
        apply_ext(l0, d0);
    }
    publish();
    simlink_progress(sl, slot, done + 1);

    while (!stop) {
        int err = 0, st;
        uint64_t m = simlink_peers_progress(sl, slot, &err), upto, tc, target;
        if (err || simlink_others(sl, slot) == 0) {
            fprintf(stderr, "petsdsim: Gegenseite beendet, tiefster SP 0x%04x\n", min_sp);
            break;
        }
        /* Weiter, wenn m - 2 + look ueber done liegt (ohne Unterlauf
           gerechnet). Direkt nach dem Anmelden meldet VICE Fortschritt 0 oder
           1; dann ist von dort noch nichts endgueltig (kein upto), die
           Vorausschau gilt aber schon. Frueher lief m - 2 dabei ueber, und
           last_ext stand danach dauerhaft auf dem Hoechstwert: petsdsim sah den
           PET nur noch an den Fortschrittsgrenzen, Zwischenzustaende fehlten. */
        if (m == UINT64_MAX || m + look <= done + 2) {
            /* Vorausschau aufgebraucht: auf die anderen warten */
            m = m == UINT64_MAX ? m : simlink_wait_peers(sl, slot, done + 2 - look);
            if (simlink_others(sl, slot) == 0) {
                fprintf(stderr, "petsdsim: Gegenseite beendet, tiefster SP 0x%04x\n", min_sp);
                break;
            }
            if (m == UINT64_MAX) {
                usleep(1000);       /* Gegenseite noch nicht aktiv */
                continue;
            }
        }
        if (m < 2) {
            target = m + look - 2;
            goto run;
        }
        /* VICE meldet Fortschritt done+1, stempelt eine spaetere Aenderung
           des PET aber noch mit done (parallel-simlink.c, publish); daher
           gilt erst alles vor m - 1 als endgueltig */
        upto = m - 2;
        /* Aenderungen der anderen bis upto vormerken; sie wirken L spaeter */
        while (pend_n < 64 && simlink_next_change(sl, slot, last_ext, upto, &tc)) {
            uint8_t d = 0;
            pend_l[pend_n] = simlink_peek_others(sl, slot, tc, &d);
            pend_d[pend_n] = d;
            pend_t[pend_n] = tc + look;
            /* bis zu einem AVR-Befehl ueber das Ziel hinaus ist unvermeidlich */
            if (pend_t[pend_n] + 1000000 < now_ps()) {
                warn_late++;
                if (warn_late <= 10 || warn_late % 1000 == 0) {
                    fprintf(stderr, "petsdsim: WARNUNG (%lu) Aenderung bei %.6f ms kommt erst bei %.6f ms an\n",
                            warn_late, tc / 1e9, now_ps() / 1e9);
                }
            }
            if (verbose > 2) {
                fprintf(stderr, "petsdsim: Aenderung bei %.3f ms: %02x/%02x\n", tc / 1e9, pend_l[pend_n], d);
            }
            pend_n++;
            last_ext = tc;
        }
        if (pend_n == 64) {
            warn_full++;
            if (warn_full <= 10 || warn_full % 1000 == 0) {
                fprintf(stderr, "petsdsim: WARNUNG (%lu) Warteschlange voll bei %.6f ms\n", warn_full, now_ps() / 1e9);
            }
        }
        if (pend_n < 64) {
            /* Ein Eintrag genau bei der gemeldeten Fortschrittsgrenze kann
               nachtraeglich erscheinen (VICE stempelt mit done_ps); daher
               zusaetzlich den Stand bei upto vergleichen */
            uint8_t d = 0, l = simlink_peek_others(sl, slot, upto, &d);
            uint8_t ql = pend_n ? pend_l[pend_n - 1] : ext_pull;
            uint8_t qd = pend_n ? pend_d[pend_n - 1] : ext_data;
            if (l != ql || d != qd) {
                warn_peek++;
                if (warn_peek <= 10 || warn_peek % 1000 == 0) {
                    fprintf(stderr, "petsdsim: WARNUNG (%lu) Zustand an der Grenze %.6f ms nur nachtraeglich gesehen (%02x/%02x, zuletzt %02x/%02x, n=%d, last_ext %.6f, m %.6f)\n",
                            warn_peek, upto / 1e9, l, d, ql, qd, pend_n, last_ext / 1e9, m / 1e9);
                }
                pend_l[pend_n] = l;
                pend_d[pend_n] = d;
                pend_t[pend_n] = upto + look;
                pend_n++;
            }
            last_ext = upto > last_ext ? upto : last_ext;
        }
        target = upto + look;
    run:
        while (now_ps() < target && !stop) {
            while (pend_n && pend_t[0] <= now_ps()) {
                apply_ext(pend_l[0], pend_d[0]);
                memmove(pend_t, pend_t + 1, sizeof pend_t[0] * (size_t)(pend_n - 1));
                memmove(pend_l, pend_l + 1, (size_t)(pend_n - 1));
                memmove(pend_d, pend_d + 1, (size_t)(pend_n - 1));
                pend_n--;
            }
            st = avr_run(avr);
            spi_fix();
            if (cap_on) {
                isr_track();
                buscap_poll(now_ps());
            }
            {
                unsigned sp = avr->data[0x5d] | (avr->data[0x5e] << 8);
                if (sp < min_sp) min_sp = sp;
            }
            if (st == cpu_Done || st == cpu_Crashed) {
                fprintf(stderr, "petsdsim: AVR angehalten (%d) bei PC %04x\n", st, avr->pc);
                stop = 1;
                break;
            }
            if (getenv_hang) hang_check();
            publish();
            panel_step();
            ee_poll();
        }
        done = now_ps();
        simlink_progress(sl, slot, done + 1);
        if (verbose) {
            static uint64_t next_rep = 0;
            if (avr->cycle >= next_rep) {
                fprintf(stderr, "petsdsim %10.3f ms: PC %05x SP %04x PIND %02x PINC %02x PORTB %02x DDRB %02x SPI %lu ss %d ADC %d ADMUX %02x\n", now_ps() / 1e9, avr->pc,
                        avr->data[0x5d] | (avr->data[0x5e] << 8), avr->data[A_PIND], avr->data[A_PINC],
                        avr->data[0x25], avr->data[0x24], spi_n, sd_ss, avr->data[0x78] | (avr->data[0x79] << 8), avr->data[0x7c]);
                next_rep = avr->cycle + cur_hz / 4;
            }
        }
    }
    if (cap_on) buscap_close();
    simlink_detach(sl, slot);
    simlink_close(sl, 0);
    ee_save();
    blk_close();
    return 0;
}
