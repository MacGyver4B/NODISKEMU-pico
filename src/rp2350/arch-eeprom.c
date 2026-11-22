/* arch-eeprom.c - Ablage des EEPROM-Abbilds (2 KB) fuer petSD2 (RP2350)

   Beim ersten Zugriff wird am I2C-Bus gesucht:
     0x57 und 0x50 antworten  -> 24C16-Typ (z. B. M24C16 der ChronoDot v3.0),
                                 Block A8-A10 in der I2C-Adresse, 1-Byte-Adresse
     nur 0x57                 -> 24C32/64 auf einem DS3231-Modul, 2-Byte-Adresse
     nur 0x50                 -> 24C32/64 auf dem Loetpad (A0-A2 an GND)
     keiner                   -> Datei PETSD2.EEP im Wurzelverzeichnis der Karte
   Genutzt werden immer 2 KB. Die Datei gilt nur ohne EEPROM; sie wird
   geladen, sobald die Karte eingebunden ist (NODISKEMU liest die
   Einstellungen danach ein zweites Mal), und geschrieben, wenn sich
   Einstellungen aendern. */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include "arch-eeprom.h"
#include "ff.h"
#include "parser.h"
#include "uart.h"
#include "pico/time.h"

int hwi2c_write(unsigned addr7, const unsigned char *buf, unsigned len, int nostop);
int hwi2c_read(unsigned addr7, unsigned char *buf, unsigned len, int nostop);

#define EE_FILE   "PETSD2.EEP"
#define EE_PAGE   16            /* 24C16: 16, 24C32: 32 -> 16 passt fuer beide */

enum { EE_UNKNOWN, EE_NONE, EE_I2C_BLOCK, EE_I2C_WIDE };

uint8_t psd2_ee_img[PSD2_EE_SIZE];
static uint8_t  ee_kind = EE_UNKNOWN;
static uint8_t  ee_addr __attribute__((unused));               /* 7-Bit-Adresse (Basis) */
static bool     ee_file_loaded;
static uint8_t  ee_dirty_pg[PSD2_EE_SIZE / EE_PAGE / 8];
static bool     ee_dirty_any;

#ifdef HAVE_I2C
static bool probe(uint8_t a) {
  uint8_t b;
  return hwi2c_read(a, &b, 1, 0) == 1;
}

/* Adresse setzen und lesen; dev und Adressbytes je nach Typ */
static bool i2c_rd(unsigned off, uint8_t *buf, unsigned n) {
  uint8_t ab[2];
  unsigned dev = ee_addr, al;
  if (ee_kind == EE_I2C_BLOCK) {
    dev = 0x50 | (off >> 8);
    ab[0] = off & 0xff; al = 1;
  } else {
    ab[0] = off >> 8; ab[1] = off & 0xff; al = 2;
  }
  if (hwi2c_write(dev, ab, al, 1) != (int)al) return false;
  return hwi2c_read(dev, buf, n, 0) == (int)n;
}

static bool i2c_wr_page(unsigned off) {
  uint8_t b[2 + EE_PAGE];
  unsigned dev = ee_addr, al;
  if (ee_kind == EE_I2C_BLOCK) {
    dev = 0x50 | (off >> 8);
    b[0] = off & 0xff; al = 1;
  } else {
    b[0] = off >> 8; b[1] = off & 0xff; al = 2;
  }
  memcpy(b + al, psd2_ee_img + off, EE_PAGE);
  if (hwi2c_write(dev, b, al + EE_PAGE, 0) != (int)(al + EE_PAGE)) return false;
  /* Schreibzyklus abwarten: der Baustein antwortet erst danach wieder */
  absolute_time_t end = make_timeout_time_ms(20);
  while (!probe(dev))
    if (time_reached(end)) return false;
  return true;
}
#endif

static void detect(void) {
  memset(psd2_ee_img, 0xff, sizeof psd2_ee_img);    /* wie ein leeres EEPROM */
  ee_kind = EE_NONE;
#ifdef HAVE_I2C
  bool a57 = probe(0x57), a50 = probe(0x50);
  if (a57 && a50)      { ee_kind = EE_I2C_BLOCK; ee_addr = 0x50; }
  else if (a57)        { ee_kind = EE_I2C_WIDE;  ee_addr = 0x57; }
  else if (a50)        { ee_kind = EE_I2C_WIDE;  ee_addr = 0x50; }
  if (ee_kind != EE_NONE) {
    bool ok = true;
    for (unsigned o = 0; ok && o < PSD2_EE_SIZE; o += 64)
      ok = i2c_rd(o, psd2_ee_img + o, 64);
    printf("EEPROM %s 0x%02x%s\r\n", ee_kind == EE_I2C_BLOCK ? "24C16" : "24C32+",
           ee_addr, ok ? "" : " Lesefehler");
    return;
  }
#endif
  printf("EEPROM keins, Datei " EE_FILE "\r\n");
}

static bool fs_ready(void) {
  return max_part > 0 && partition[0].fatfs != NULL;
}

void psd2_ee_sync_in(void) {
  if (ee_kind == EE_UNKNOWN)
    detect();
  if (ee_kind == EE_NONE && !ee_file_loaded && fs_ready()) {
    FIL f;
    UINT got = 0;
    ee_file_loaded = true;
    if (f_open(partition[0].fatfs, &f, (const UCHAR *)EE_FILE, FA_READ) == FR_OK) {
      static uint8_t tmp[PSD2_EE_SIZE];
      if (f_read(&f, tmp, sizeof tmp, &got) == FR_OK && got == sizeof tmp) {
        memcpy(psd2_ee_img, tmp, sizeof tmp);
        memset(ee_dirty_pg, 0, sizeof ee_dirty_pg);
        ee_dirty_any = false;
      }
      f_close(&f);
    }
    printf("EEPROM-Datei %s\r\n", got == PSD2_EE_SIZE ? "geladen" : "fehlt");
  }
}

void psd2_ee_dirty(unsigned off, unsigned n) {
  for (unsigned i = 0; i < n; i++) {
    unsigned pg = ((off + i) & (PSD2_EE_SIZE - 1)) / EE_PAGE;
    ee_dirty_pg[pg >> 3] |= 1u << (pg & 7);
  }
  ee_dirty_any = true;
}

void psd2_ee_flush(void) {
  if (!ee_dirty_any)
    return;
  if (ee_kind == EE_UNKNOWN)
    detect();
#ifdef HAVE_I2C
  if (ee_kind == EE_I2C_BLOCK || ee_kind == EE_I2C_WIDE) {
    for (unsigned pg = 0; pg < PSD2_EE_SIZE / EE_PAGE; pg++)
      if (ee_dirty_pg[pg >> 3] & (1u << (pg & 7))) {
        if (!i2c_wr_page(pg * EE_PAGE)) {
          printf("EEPROM Schreibfehler bei %u\r\n", pg * EE_PAGE);
          return;                       /* bleibt markiert */
        }
        ee_dirty_pg[pg >> 3] &= ~(1u << (pg & 7));
      }
    ee_dirty_any = false;
    return;
  }
#endif
  /* Datei: nur mit eingebundener Karte; sonst beim naechsten Mal */
  if (!fs_ready())
    return;
  ee_file_loaded = true;      /* ab jetzt gilt das Abbild im RAM */
  FIL f;
  UINT put = 0;
  if (f_open(partition[0].fatfs, &f, (const UCHAR *)EE_FILE, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
    f_write(&f, psd2_ee_img, PSD2_EE_SIZE, &put);
    f_close(&f);
  }
  if (put == PSD2_EE_SIZE) {
    memset(ee_dirty_pg, 0, sizeof ee_dirty_pg);
    ee_dirty_any = false;
  }
  printf("EEPROM-Datei %s\r\n", put == PSD2_EE_SIZE ? "geschrieben" : "Schreibfehler");
}
