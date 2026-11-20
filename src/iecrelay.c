/* iecrelay.c - Relais IEEE-488 (PET) -> IEC (serielle Laufwerke) fuer petSD2

   Portierung der Protokollebene aus simlink/test/relay.c (VICE-Fork): kein
   eigenes DOS, Befehle, Dateinamen und Daten werden weitergereicht.

   - PET sendet (OPEN, Befehle, SAVE): Bytes sofort annehmen; Namen und
     Befehle gehen bei UNLISTEN weiter, Schreibdaten in Portionen von 254
     Bytes (NRFD bleibt derweil gehalten, der PET wartet ohne Frist).
   - PET empfaengt (LOAD, GET#, Fehlerkanal): vor der Annahme des Sekundaer-
     befehls nach TALK den Kanal ueber IEC holen (ieee.c liest das Byte bei
     gehaltenem NRFD von den Datenleitungen), danach sofort liefern. Keine
     Daten: nichts liefern, der PET meldet ?FILE NOT FOUND.

   Laeuft synchron auf Kern 0, der IEEE-Bus steht derweil (NRFD/NDAC). */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "config.h"
#include "pico/platform.h"   /* __not_in_flash_func: Zeitkritisches im RAM */
#include "uart.h"
#include "iecrelay.h"
#include "iec-host.h"
#include "timer.h"

#define CHUNK 254
#define BUF0  65536             /* Kanal 0 (LOAD) ganz, fuer Schnelllader */

uint8_t relay_ieee_unit = 9;    /* Adresse am IEEE-Bus */
uint8_t relay_iec_unit  = 8;    /* Laufwerk am IEC-Bus */

static uint8_t pull;            /* eigene IEC-Leitungen (gesetzt = low) */
static int iec_err;
static int c_fail;
static volatile int getenv_pio_test = 1;                     /* Diagnose: wo das Lesen ausstieg */

/* ----------------------------- Zeit ----------------------------------- */

static void __not_in_flash_func(wait_us)(unsigned n) {
  uint64_t end = iec_now() + (uint64_t)n * iec_cyc_per_us;
  while (iec_now() < end)
    ;
}

static void __not_in_flash_func(set)(uint8_t p) {
  pull = p;
  iec_out(p);
  if (iec_pio_mode)
    iec_pio_end();                      /* SIO steht schon richtig */
}

/* nach dem Freigeben einer Leitung, bevor geprueft wird, ob die Gegenseite
   sie zieht: Anstiegszeit (Pull-ups, Kabel) und Gatterlaufzeit abwarten,
   sonst liest man noch den eigenen low-Pegel */
#define IEC_SETTLE_US 5
static void __not_in_flash_func(settle)(void) { wait_us(IEC_SETTLE_US); }

static void __not_in_flash_func(c_on)(uint8_t m)  { set(pull | m); }
static void __not_in_flash_func(c_off)(uint8_t m) { set((uint8_t)(pull & ~m)); }

/* warten, bis (bus & m) == want; 0 ok, -1 Frist (0 = ohne Frist) */
static int __not_in_flash_func(c_wait)(uint8_t m, uint8_t want, unsigned timeout_us) {
  uint64_t dl = timeout_us ? iec_now() + (uint64_t)timeout_us * iec_cyc_per_us : 0;
  for (;;) {
    if ((iec_in() & m) == want)
      return 0;
    if (dl && iec_now() >= dl)
      return -1;
  }
}

/* -------------------------- IEC-Rechnerseite -------------------------- */

static int __not_in_flash_func(c_send)(uint8_t b, int eoi) {
  c_off(IEC_CLK);
  if (c_wait(IEC_DATA, 0, 0) < 0)
    return -1;
  if (eoi) {
    if (c_wait(IEC_DATA, IEC_DATA, 1000) < 0 || c_wait(IEC_DATA, 0, 1000) < 0)
      return -1;
  }
  wait_us(20);
  c_on(IEC_CLK);
  wait_us(80);
  for (int i = 0; i < 8; i++) {
    if (b & (1 << i)) c_off(IEC_DATA);
    else              c_on(IEC_DATA);
    wait_us(30);
    c_off(IEC_CLK);
    wait_us(60);
    c_on(IEC_CLK);
    c_off(IEC_DATA);
    wait_us(60);
  }
  return c_wait(IEC_DATA, IEC_DATA, 1000);
}

static int __not_in_flash_func(c_atn)(uint8_t b1, int b2) {
  c_on(IEC_ATN | IEC_CLK);
  c_off(IEC_DATA);
  settle();
  if (c_wait(IEC_DATA, IEC_DATA, 2000) < 0) {
    c_off(IEC_ATN | IEC_CLK);
    return -1;                          /* Geraet nicht vorhanden */
  }
  wait_us(100);
  if (c_send(b1, 0) < 0 || (b2 >= 0 && c_send((uint8_t)b2, 0) < 0)) {
    c_off(IEC_ATN | IEC_CLK);
    return -1;
  }
  return 0;
}

static void __not_in_flash_func(c_atn_end)(void) {
  wait_us(20);
  c_off(IEC_ATN);
  wait_us(100);
}

static int c_listen(int sec) {
  if (c_atn((uint8_t)(0x20 + relay_iec_unit), sec) < 0)
    return -1;
  c_atn_end();
  return 0;
}

static void c_unlisten(void) {
  if (c_atn(0x3f, -1) == 0)
    c_atn_end();
  c_off(IEC_CLK | IEC_DATA);
}

static int __not_in_flash_func(c_recv)(int *eoi) {
  int b = 0;
  *eoi = 0;
  if (c_wait(IEC_CLK, 0, 0) < 0)
    return -1;
  c_off(IEC_DATA);
  if (c_wait(IEC_CLK, IEC_CLK, 200) < 0) {
    *eoi = 1;
    c_on(IEC_DATA);
    wait_us(60);
    c_off(IEC_DATA);
    if (c_wait(IEC_CLK, IEC_CLK, 64000) < 0) {
      c_fail = 1;
      return -1;                        /* Sprecher schweigt (Datei fehlt) */
    }
  }
  for (int i = 0; i < 8; i++) {
    if (c_wait(IEC_CLK, 0, 2000) < 0) { c_fail = 10 + i; return -1; }
    if (!(iec_in() & IEC_DATA))
      b |= 1 << i;
    if (c_wait(IEC_CLK, IEC_CLK, 2000) < 0) { c_fail = 20 + i; return -1; }
  }
  c_on(IEC_DATA);
  return b;
}

/* Kanal sa lesen: TALK, bis EOI oder max Bytes, UNTALK */
static size_t __not_in_flash_func(c_read_channel)(int sa, uint8_t *buf, size_t max, int *got_eoi) {
  size_t n = 0;
  int eoi = 0, b;

  c_fail = 0;
  if (c_atn((uint8_t)(0x40 + relay_iec_unit), 0x60 | sa) < 0) {
    iec_err = 1;
    *got_eoi = 0;
    c_fail = 2;
    return 0;
  }
  c_on(IEC_DATA);                       /* Umschalten: wir werden Hoerer */
  c_off(IEC_ATN);
  wait_us(20);
  c_off(IEC_CLK);
  settle();
  if (c_wait(IEC_CLK, IEC_CLK, 2000) < 0) {
    iec_err = 1;
    c_fail = 3;
  } else {
    while (!eoi && n < max) {
      b = c_recv(&eoi);
      if (b < 0)
        break;
      buf[n++] = (uint8_t)b;
    }
    wait_us(100);                       /* letzte Quittung stehen lassen */
  }
  if (c_atn(0x5f, -1) == 0)
    c_atn_end();
  c_off(IEC_CLK | IEC_DATA);
  *got_eoi = eoi;
  return n;
}

/* LISTEN sec, n Bytes, UNLISTEN; EOI auf dem letzten nur mit eoi_last */
static void c_write(int sec, const uint8_t *s, size_t n, int eoi_last) {
  if (c_listen(sec) < 0) {
    iec_err = 1;
    return;
  }
  for (size_t i = 0; i < n; i++) {
    if (c_send(s[i], eoi_last && i + 1 == n) < 0) {
      iec_err = 1;
      break;
    }
  }
  c_unlisten();
}

static void c_close(int sa) {
  if (c_atn((uint8_t)(0x20 + relay_iec_unit), 0xe0 | sa) == 0)
    c_atn_end();
  c_unlisten();
}

/* ------------------------------ Kanaele ------------------------------- */

static uint8_t buf0[BUF0];
static uint8_t bufn[16][256];
static size_t clen[16], cpos[16];
static bool cvalid[16], ceof[16];

static uint8_t *cbuf(int sa) { return sa == 0 ? buf0 : bufn[sa]; }


/* ================= AR6-Schnelllader (aus relay.c) ======================
   Host-Seite des Action Replay 6 (V6.0 PAL, bank2.s): Laufwerkscode
   hochladen, Spuren mit $E0 lesen, Verzeichnis und Kette hier verfolgen;
   SAVE ueber den Schreibcode. Der Laufwerkscode (Bank 2) kommt zur Laufzeit
   aus AR6.CRT auf der SD-Karte und ist nicht Teil der Firmware.
   Leitung gesetzt = low, Zeiten in us ab eigenem Schreiben bzw. Flanke. */

#include "ff.h"
#include "parser.h"

static uint8_t ar_rom[8192];            /* Bank 2, $8000-$9FFF */
static bool ar_ok;                      /* CRT geladen */
#define AR(a) (ar_rom + ((a) - 0x8000))
#define US(n) ((uint64_t)(n) * iec_cyc_per_us)

static unsigned ar_gap = 30;
static bool ar_pending, ar_loaded, ar_save;
static uint8_t ar_name[256];
static size_t ar_namelen;
static uint8_t ar_sbuf[BUF0];
static size_t ar_slen;
static uint8_t ar_trk[36][21][256];
static uint8_t ar_have[36][21];
static unsigned ar_fallback;
static int ar_first;
static unsigned ar_p0 = 14;             /* erste Abtastung nach W: 1541 +14, 1581 +10 */
static const char *ar_why = "";
static int ar_wtr, ar_ws, ar_cks;
static int ar_last_ty;                 /* Typ beim letzten AR6-SAVE */

static void ar_load_crt(void) {
  FIL f;
  UINT got;
  uint8_t h[64], ch[16];
  ar_ok = false;
  if (f_open(partition[0].fatfs, &f, (const UCHAR *)"AR6.CRT", FA_READ) != FR_OK)
    return;
  if (f_read(&f, h, 64, &got) != FR_OK || got != 64 || memcmp(h, "C64 CARTRIDGE", 13))
    goto out;
  DWORD pos = ((DWORD)h[0x10] << 24) | ((DWORD)h[0x11] << 16) | (h[0x12] << 8) | h[0x13];
  for (;;) {
    if (f_lseek(&f, pos) != FR_OK || f_read(&f, ch, 16, &got) != FR_OK || got != 16)
      break;
    DWORD ln = ((DWORD)ch[4] << 24) | ((DWORD)ch[5] << 16) | (ch[6] << 8) | ch[7];
    int bank = (ch[0x0a] << 8) | ch[0x0b];
    if (bank == 2 && ln >= 16 + 8192) {
      if (f_read(&f, ar_rom, 8192, &got) == FR_OK && got == 8192)
        ar_ok = true;
      break;
    }
    if (ln < 16)
      break;
    pos += ln;
  }
out:
  f_close(&f);
  printf("relay: AR6.CRT %s\r\n", ar_ok ? "geladen, Schnelllader aktiv" : "fehlt, Standardprotokoll");
}

/* bis tp warten */
static void __not_in_flash_func(ar_at)(uint64_t tp) {
  while (iec_now() < tp)
    ;
}

/* warten, bis (in & m) == want; Zeit der beobachteten Flanke, 0 bei Frist */
static uint64_t __not_in_flash_func(ar_until)(uint8_t m, uint8_t want, unsigned timeout_us) {
  uint64_t dl = iec_now() + US(timeout_us), n;
  for (;;) {
    n = iec_now();
    if ((iec_in() & m) == want)
      return n;
    if (n >= dl)
      return 0;
  }
}

static int __not_in_flash_func(ar_ready)(unsigned timeout_us) {
  if (!(iec_in() & IEC_CLK))
    ar_until(IEC_CLK, IEC_CLK, 50000);  /* erst low (beschaeftigt) */
  return ar_until(IEC_CLK, 0, timeout_us) ? 0 : -1;
}

static uint8_t __not_in_flash_func(ar_pair)(int n, int first) {
  uint8_t p = 0;
  if (first) {
    if (n & 8) p |= IEC_CLK;
    if (n & 2) p |= IEC_DATA;
  } else {
    if (n & 4) p |= IEC_CLK;
    if (n & 1) p |= IEC_DATA;
  }
  return p;
}

/* Rechner -> Laufwerk, 1 Byte: Paare bei +8/+18/+34/+44, DATA low bei +57.
   Die Zeitfolge erzeugt die PIO (ar6put.pio), die CPU legt nur das Wort an. */
static uint32_t lv(uint8_t p) {         /* Pegel: Bit 0 CLK, Bit 1 DATA, 1 = frei */
  return (p & IEC_CLK ? 0 : 1) | (p & IEC_DATA ? 0 : 2);
}

static void __not_in_flash_func(ar_put)(uint8_t b) {
  uint32_t w = lv(0) | lv(ar_pair(b >> 4, 1)) << 2 | lv(ar_pair(b >> 4, 0)) << 4 |
               lv(ar_pair(b & 15, 1)) << 6 | lv(ar_pair(b & 15, 0)) << 8 | lv(IEC_DATA) << 10;
  if (!iec_pio_mode)
    iec_pio_begin(lv(pull));
  iec_pio_put(w);
  pull = IEC_DATA;
  iec_out(IEC_DATA);                    /* SIO auf denselben Stand fuer die Rueckgabe */
}

static int __not_in_flash_func(ar_send)(uint8_t b) {
  if (ar_ready(2000000) < 0)
    return -1;
  ar_put(b);
  return 0;
}

/* Abtastwert: Bit 1 = DATA high, Bit 0 = CLK high */
static int __not_in_flash_func(ar_sample)(uint64_t tp) {
  ar_at(tp);
  uint8_t in = iec_in();
  return (in & IEC_DATA ? 0 : 2) | (in & IEC_CLK ? 0 : 1);
}

/* Laufwerk -> Rechner, 1 Byte mit Handshake (siehe relay.c) */
static int __not_in_flash_func(ar_get)(unsigned timeout_us) {
  uint64_t w;
  int i, b = 0;
  if (!(iec_in() & IEC_CLK) &&
      !ar_until(IEC_CLK, IEC_CLK, ar_p0 == 10 && !ar_first ? 100 : 50000)) {
    /* Laufwerk war schon bereit: eigene Freigabe als Bezug */
    uint64_t t0 = iec_now() + US(1);
    unsigned p = ar_p0 == 10 ? 9 : 14;
    ar_fallback++;
    ar_at(t0);
    set(0);
    for (i = 0; i < 4; i++)
      b |= ar_sample(t0 + US(p + 8 * i)) << (2 * i);
    ar_at(t0 + US(p + 26));
    set(IEC_DATA);
    return b;
  }
  set(0);
  ar_first = 0;
  if (!(w = ar_until(IEC_CLK, 0, timeout_us))) {
    set(IEC_DATA);
    return -1;
  }
  for (i = 0; i < 4; i++)
    b |= ar_sample(w + US(ar_p0 + 8 * i)) << (2 * i);
  ar_at(w + US(ar_p0 + 26));
  set(IEC_DATA);
  return b;
}

/* Block 256 Byte ohne Handshake, danach Pruefbyte; 0 ok */
static int __not_in_flash_func(ar_block)(uint8_t *buf) {
  uint64_t r;
  int x = 0, c;
  ar_at(iec_now() + US(15));
  set(0);
  if (!ar_until(IEC_CLK, 0, 5000) || !ar_until(IEC_CLK, IEC_CLK, 100) ||
      !ar_until(IEC_CLK, 0, 100) || !ar_until(IEC_CLK, IEC_CLK, 100) ||
      !(r = ar_until(IEC_CLK, 0, 100)))
    return -1;
  for (int n = 0; n < 64; n++) {
    for (int k = 0; k < 4; k++) {
      uint64_t t0 = r + US(179 * n + 12 + 40 * k);
      int b = ar_sample(t0 + US(4)) | ar_sample(t0 + US(14)) << 2 |
              ar_sample(t0 + US(24)) << 4 | ar_sample(t0 + US(34)) << 6;
      buf[n * 4 + k] = (uint8_t)b;
      x ^= b;
    }
  }
  c = ar_get(2000);
  if (c < 0)
    return -1;
  return (x ^ c) ? -2 : 0;
}

static int ar_sectors(int tr) {
  return tr < 18 ? 21 : tr < 25 ? 19 : tr < 31 ? 18 : 17;
}

static int ar_track(int tr) {
  uint8_t buf[256];
  int s, cnt, got = 0, e;
  if (tr < 1 || tr > 35)
    return -1;
  ar_wtr = tr;
  if (ar_send(0xe0) < 0 || ar_send((uint8_t)tr) < 0 || ar_send(0) < 0) {
    ar_why = "Spur: Befehl nicht angenommen";
    return -1;
  }
  cnt = ar_get(3000000);
  if (cnt < 0 || cnt >= 0x16) {
    ar_why = "Spur: Fehlercode"; ar_ws = cnt;
    return -1;
  }
  for (;;) {
    s = ar_get(500000);
    if (s < 0) {
      ar_why = "Spur: keine Sektornummer";
      return -1;
    }
    if (s >= 0x15)
      break;
    e = ar_block(buf);
    if (e == 0) {
      memcpy(ar_trk[tr][s], buf, 256);
      ar_have[tr][s] = 1;
      got++;
    } else if (e == -1) {
      ar_why = "Spur: kein Block"; ar_ws = s;
      return -1;
    } else {
      ar_cks++;
      printf("relay AR6 Spur %d Sektor %d: Pruefsumme falsch\r\n", tr, s);
    }
  }
  return got;
}

static uint8_t *ar_sector(int tr, int s) {
  if (tr < 1 || tr > 35 || s < 0 || s >= ar_sectors(tr))
    return NULL;
  for (int tries = 0; !ar_have[tr][s] && tries < 3; tries++)
    if (ar_track(tr) < 0)
      return NULL;
  return ar_have[tr][s] ? ar_trk[tr][s] : NULL;
}

static void ar_mw(int addr, const uint8_t *src, int n) {
  uint8_t cmd[64];
  while (n > 0) {
    int k = n < 35 ? n : 35;
    cmd[0] = 'M'; cmd[1] = '-'; cmd[2] = 'W';
    cmd[3] = (uint8_t)addr; cmd[4] = (uint8_t)(addr >> 8); cmd[5] = (uint8_t)k;
    memcpy(cmd + 6, src, (size_t)k);
    c_write(0x6f, cmd, (size_t)(6 + k), 1);
    addr += k; src += k; n -= k;
  }
}

static int ar_upload_x(int src2, int pages, int start) {
  uint8_t me[] = { 'M', '-', 'E', 0x50, 0x01, 0, 0, 0, 0x2c, 0xa0 };
  me[5] = (uint8_t)pages;
  me[6] = (uint8_t)start;
  me[7] = (uint8_t)(start >> 8);
  iec_err = 0;
  ar_mw(0x0150, AR(0x8a2e), 70);
  c_write(0x6f, me, sizeof me, 1);
  if (iec_err)
    return -1;
  set(IEC_DATA);
  for (int p = 0; p <= pages; p++) {
    const uint8_t *src = p == 0 ? AR(0x93cd) : AR(src2 + 256 * (p - 1));
    if (ar_ready(200000) < 0) {
      set(0);
      ar_why = "Upload: Laufwerk meldet sich nicht"; ar_ws = p;
      return -1;
    }
    for (int i = 0; i < 256; i++) {
      ar_put(src[i]);
      ar_at(iec_now() + US(ar_gap));
    }
  }
  ar_loaded = true;
  return 0;
}

static void ar_exit(uint8_t code) {
  if (ar_loaded) {
    ar_send(code);
    ar_until(IEC_CLK, IEC_CLK, 2000);
    set(0);
    ar_at(iec_now() + US(2000));
    ar_loaded = false;
  }
}

static int ar_match(const uint8_t *pat, size_t pn, const uint8_t *e) {
  for (size_t i = 0; i < 16; i++) {
    if (i < pn && pat[i] == '*')
      return 1;
    if (i >= pn)
      return e[i] == 0xa0;
    if (pat[i] != '?' && pat[i] != e[i])
      return 0;
  }
  return pn <= 16;
}

/* Laufwerkstyp bei jedem Zugriff: M-R $FFFE ($67 = 1541-Familie, $03 = 1581),
   dann $FF79 ($AA = 1541, $E6 = 1570/1571) */
static int ar_detect(void) {
  static const uint8_t mr[]  = { 'M', '-', 'R', 0xfe, 0xff, 1 };
  static const uint8_t mr2[] = { 'M', '-', 'R', 0x79, 0xff, 1 };
  uint8_t b[8];
  int eoi = 0;
  iec_err = 0;
  set(0);
  c_write(0x6f, mr, sizeof mr, 1);
  if (iec_err)
    return 0;
  size_t n = c_read_channel(15, b, 1, &eoi);
  cvalid[15] = false;
  if (n < 1)
    return 0;
  if (b[0] == 0x03)
    return 1581;
  if (b[0] != 0x67)
    return 0;
  c_write(0x6f, mr2, sizeof mr2, 1);
  n = c_read_channel(15, b, 1, &eoi);
  cvalid[15] = false;
  return n && b[0] == 0xe6 ? 1571 : 1541;
}

static long ar_loadfile(const uint8_t *nm, size_t nn, uint8_t *out, size_t max) {
  const uint8_t *sec, *p = nm;
  int tr = 18, s = 1, hops = 0, ft = -1, fs = 0;
  long n = 0;

  if (nn >= 2 && p[1] == ':') { p += 2; nn -= 2; }
  else if (nn >= 1 && p[0] == ':') { p++; nn--; }
  for (size_t i = 0; i < nn; i++)
    if (p[i] == ',') nn = i;
  memset(ar_have, 0, sizeof ar_have);
  if (ar_upload_x(0x94cc, 2, 0x0331) < 0)
    return -1;
  while (tr && ft < 0 && hops++ < 20) {
    if (!(sec = ar_sector(tr, s))) { ar_exit(0); return -1; }
    for (int i = 0; i < 8; i++) {
      const uint8_t *e = sec + 32 * i;
      if ((e[2] & 0x87) == 0x82 && ar_match(p, nn, e + 5)) {
        ft = e[3]; fs = e[4];
        break;
      }
    }
    tr = sec[0]; s = sec[1];
  }
  if (ft < 0) {
    ar_exit(0x62);
    return 0;
  }
  tr = ft; s = fs; hops = 0;
  while (tr && hops++ < 800) {
    if (!(sec = ar_sector(tr, s))) { ar_exit(0); return -1; }
    int k = sec[0] ? 254 : (sec[1] >= 1 ? sec[1] - 1 : 0);
    if ((size_t)(n + k) > max) { ar_exit(0); return -1; }
    memcpy(out + n, sec + 2, (size_t)k);
    n += k;
    tr = sec[0]; s = sec[1];
  }
  ar_exit(0);
  return n;
}

static long ar_load1581(const uint8_t *nm, size_t nn, uint8_t *out, size_t max) {
  static const uint8_t me[] = { 'M', '-', 'E', 0x00, 0x05 };
  uint8_t hd[4];
  int eoi = 0, c, k;
  long n = 0;

  iec_err = 0;
  c_write(0xf0, nm, nn, 1);
  if (c_read_channel(0, hd, 2, &eoi) < 2 || eoi) {
    c_listen(0xe0);
    c_unlisten();
    return -1;
  }
  ar_mw(0x0500, AR(0x99b0), 315);
  c_write(0x6f, me, sizeof me, 1);
  if (iec_err) {
    c_listen(0xe0);
    c_unlisten();
    return -1;
  }
  set(IEC_DATA);
  ar_p0 = 10;
  ar_first = 1;
  ar_fallback = 0;
  for (;;) {
    c = ar_get(3000000);
    if (c < 0) { n = -1; break; }
    if (c == 0) break;
    for (int i = 0; i < c; i++) {
      k = ar_get(200000);
      if (k < 0 || (size_t)n >= max) { n = -1; break; }
      out[n++] = (uint8_t)k;
    }
    if (n < 0) break;
  }
  ar_p0 = 14;
  set(0);
  ar_at(iec_now() + US(2000));
  c_listen(0xe0);
  c_unlisten();
  return n;
}

static void ar_errchan(void) {
  int eoi = 0;
  set(0);
  clen[15] = c_read_channel(15, bufn[15], 255, &eoi);
  cpos[15] = 0;
  cvalid[15] = true;
  ceof[15] = true;
}

/* 0 ok, 1 Fehler gemeldet (Fehlerkanal bereit), -1 AR6 nicht moeglich */
static int ar_savefile(const uint8_t *nm, size_t nn, const uint8_t *d, size_t len) {
  uint8_t cmd[260], blk[256];
  size_t n, r;

  int ty;
  if (len < 2)
    return -1;
  ty = ar_last_ty = ar_detect();
  if (ty == 1571) {
    /* doppelseitige Diskette: im 1541-Modus wuerde die BAM der zweiten
       Seite nicht gepflegt, also normal speichern. BAM-Byte 3 ($0703 nach
       I0) hat Bit 7 bei doppelseitig. */
    static const uint8_t mr[] = { 'M', '-', 'R', 0x03, 0x07, 1 };
    uint8_t b0 = 0x80;
    int eoi = 0;
    c_write(0x6f, (const uint8_t *)"I0", 2, 1);
    c_write(0x6f, mr, sizeof mr, 1);
    if (c_read_channel(15, &b0, 1, &eoi) != 1 || (b0 & 0x80)) {
      cvalid[15] = false;
      ar_last_ty = 0;
      return -1;
    }
    cvalid[15] = false;
    c_write(0x6f, (const uint8_t *)"U0>M0", 5, 1);   /* 1541-Modus wie beim LOAD */
  } else if (ty != 1541) {
    return -1;
  }
  n = (len + 253) / 254;
  if (n > 255)
    return -1;
  if (nn && nm[0] == '@') {               /* ersetzen: erst loeschen */
    cmd[0] = 'S';
    memcpy(cmd + 1, nm + 1, nn - 1);
    c_write(0x6f, cmd, nn, 1);
    nm++;
    nn--;
  }
  iec_err = 0;
  c_write(0xf1, nm, nn, 1);
  c_write(0x61, d, 2, 1);
  ar_errchan();
  if (iec_err || clen[15] < 2 || bufn[15][0] != '0' || bufn[15][1] != '0')
    return 1;
  if (ar_upload_x(0x8e65, 2, 0x044f) < 0)
    return -1;
  ar_send((uint8_t)n);
  ar_at(iec_now() + US(1300));            /* CLK frei = Diskette voll */
  if (!(iec_in() & IEC_CLK)) {
    ar_loaded = false;
    set(0);
    ar_at(iec_now() + US(5000));
    ar_errchan();
    return 1;
  }
  r = len - 254 * (n - 1);
  for (size_t i = 0; i < n; i++) {
    size_t m = i + 1 < n ? 254 : r;
    memset(blk, 0, sizeof blk);
    blk[0] = (uint8_t)(n - 1 - i);
    blk[1] = (uint8_t)(r + 2);
    memcpy(blk + 2, d + 254 * i, m);
    if (ar_ready(3000000) < 0) {
      ar_loaded = false;
      set(0);
      ar_errchan();
      return 1;
    }
    for (int k = 0; k < 256; k++) {
      ar_put(blk[k]);
      ar_at(iec_now() + US(ar_gap));
    }
  }
  ar_until(IEC_CLK, 0, 3000000);
  ar_at(iec_now() + US(1000));
  set(0);
  ar_loaded = false;
  ar_at(iec_now() + US(1000));
  cvalid[15] = false;
  return 0;
}

static inline uint32_t irq_off(void) {
  uint32_t s;
  __asm volatile ("mrs %0, primask\n cpsid i" : "=r" (s) :: "memory");
  return s;
}
static inline void irq_restore(uint32_t s) {
  __asm volatile ("msr primask, %0" :: "r" (s) : "memory");
}

/* LOAD per Schnelllader; Anzahl Bytes, 0 = Datei fehlt, -1 = normal laden */
static long ar_fastload(void) {
  uint64_t t0 = iec_now();
  uint32_t irq = irq_off();               /* Zeitfenster im us-Bereich */
  int ty = ar_detect();
  long n = -1;
  if (ty == 1541 || ty == 1571) {
    if (ty == 1571)
      c_write(0x6f, (const uint8_t *)"U0>M0", 5, 1);   /* 1541-Modus */
    n = ar_loadfile(ar_name, ar_namelen, buf0, BUF0);
    if (ty == 1571) {
      set(0);
      c_write(0x6f, (const uint8_t *)"U0>M1", 5, 1);   /* zurueck auf 1571 */
    }
  } else if (ty == 1581) {
    n = ar_load1581(ar_name, ar_namelen, buf0, BUF0);
  }
  irq_restore(irq);
  uint64_t dt = (iec_now() - t0) / iec_cyc_per_us;
  if (n >= 0)
    printf("relay AR6 %d: %ld Bytes in %lu ms (%lu B/s), %u ohne Flanke\r\n", ty, n,
           (unsigned long)(dt / 1000), (unsigned long)(dt ? (uint64_t)n * 1000000 / dt : 0), ar_fallback);
  else
    printf("relay AR6 %d: nicht moeglich (%s, Spur %d, %d), lade normal\r\n", ty, ar_why, ar_wtr, ar_ws);
  return n;
}

static int opening = -1;                /* OPEN laeuft fuer Kanal n */
static uint8_t name[256];
static size_t namelen;
static int lsa;                         /* Kanal der laufenden Listen-Phase */
static uint8_t wbuf[CHUNK + 1];
static size_t wlen;

void relay_init(void) {
  iec_host_init();
  set(0);
  memset(cvalid, 0, sizeof cvalid);
  opening = -1;
  ar_load_crt();
}

/* Laufwerk am IEC-Bus vorhanden? Kurzer LISTEN/UNLISTEN; ein "ja" gilt
   1 s (byteweises Lesen), ein "nein" wird bei jedem Zugriff neu geprueft,
   damit ein spaeter eingeschaltetes Laufwerk sofort erkannt wird. */
bool relay_present(void) {
  static uint64_t until;
  uint64_t now = iec_now();
  if (until && now < until)
    return true;
  if (c_atn((uint8_t)(0x20 + relay_iec_unit), -1) < 0) {
    until = 0;
    return false;
  }
  c_atn_end();
  c_unlisten();
  until = now + (uint64_t)1000000 * iec_cyc_per_us;
  return true;
}

void relay_open_begin(uint8_t sa) {
  opening = sa;
  namelen = 0;
  lsa = sa;
}

void relay_data_begin(uint8_t sa) {
  opening = -1;
  lsa = sa;
  wlen = 0;
}

void relay_rx(uint8_t b) {
  if (opening >= 0) {
    if (namelen < sizeof name)
      name[namelen++] = b;
    return;
  }
  if (ar_save && lsa == 1) {
    if (ar_slen < sizeof ar_sbuf)
      ar_sbuf[ar_slen++] = b;
    return;
  }
  wbuf[wlen++] = b;
  /* Schreibdaten (nicht Befehle) in Portionen weiterreichen; ein Byte
     bleibt zurueck, damit das letzte bei UNLISTEN mit EOI rausgeht */
  if (lsa != 15 && wlen == CHUNK + 1) {
    c_write(0x60 | lsa, wbuf, CHUNK, 0);
    wbuf[0] = wbuf[CHUNK];
    wlen = 1;
  }
}

void relay_unlisten(void) {
  if (opening == 0 && ar_ok && namelen && name[0] != '$') {
    memcpy(ar_name, name, namelen);
    ar_namelen = namelen;
    ar_pending = true;
    cvalid[0] = cvalid[15] = false;
    opening = -1;
  } else if (opening == 1 && ar_ok && namelen) {
    memcpy(ar_name, name, namelen);
    ar_namelen = namelen;
    ar_save = true;
    ar_slen = 0;
    opening = -1;
  } else if (opening >= 0) {
    printf("relay OPEN %d (%u Zeichen)\r\n", opening, (unsigned)namelen);
    c_write(0xf0 | opening, name, namelen, 1);
    cvalid[opening] = false;
    if (namelen)
      cvalid[15] = false;               /* OPEN mit Namen setzt Fehlerkanal neu */
    opening = -1;
  } else if (wlen) {
    iec_err = 0;
    c_write(0x60 | lsa, wbuf, wlen, 1);
    wlen = 0;
    if (lsa == 15)
      cvalid[15] = false;               /* Befehl: Fehlerkanal neu holen */
  }
}

void relay_close(uint8_t sa) {
  printf("relay CLOSE %d\r\n", sa);
  if (sa == 1 && ar_save) {
    ar_save = false;
    uint64_t t0 = iec_now();
    uint32_t irq = irq_off();
    ar_last_ty = 0;
    int e = ar_savefile(ar_name, ar_namelen, ar_sbuf, ar_slen);
    if (ar_last_ty == 1571) {
      set(0);
      c_write(0x6f, (const uint8_t *)"U0>M1", 5, 1);   /* zurueck auf 1571 */
    }
    if (e < 0) {                          /* normal speichern */
      set(0);
      c_write(0xf1, ar_name, ar_namelen, 1);
      c_write(0x61, ar_sbuf, ar_slen, 1);
      c_close(1);
    }
    irq_restore(irq);
    uint64_t dt = (iec_now() - t0) / iec_cyc_per_us;
    printf("relay SAVE %s: %u Bytes in %lu ms\r\n", e < 0 ? "normal" : e ? "Fehler" : "AR6",
           (unsigned)ar_slen, (unsigned long)(dt / 1000));
    cvalid[15] = e > 0;
    cvalid[1] = false;
    return;
  }
  c_close(sa);
  cvalid[sa] = false;
}

/* Lesekanal bereitstellen (vor der Annahme des TALK-Sekundaerbefehls) */
void relay_prefetch(uint8_t sa) {
  int eoi = 0;
  if (cvalid[sa] && cpos[sa] < clen[sa])
    return;
  if (sa != 15 && cvalid[sa] && ceof[sa])
    return;
  if (sa == 0 && ar_pending) {
    ar_pending = false;
    long n = ar_fastload();
    if (n >= 0) {
      clen[0] = (size_t)n;
      cpos[0] = 0;
      cvalid[0] = ceof[0] = true;
      cvalid[15] = false;
      return;
    }
    set(0);                               /* gescheitert: normal oeffnen */
    c_write(0xf0, ar_name, ar_namelen, 1);
  }
  iec_err = 0;
  clen[sa] = c_read_channel(sa, cbuf(sa), sa == 15 ? 255 : CHUNK, &eoi);
  cpos[sa] = 0;
  cvalid[sa] = true;
  ceof[sa] = (sa == 15 || eoi || clen[sa] < CHUNK);
  printf("relay Kanal %d: %u Bytes%s\r\n", sa, (unsigned)clen[sa], ceof[sa] ? " (Ende)" : "");
}

/* naechstes Byte fuer den PET; false = nichts zu liefern */
bool relay_peek(uint8_t sa, uint8_t *b, bool *eoi) {
  if (cvalid[sa] && cpos[sa] >= clen[sa] && !ceof[sa])
    relay_prefetch(sa);                 /* naechste Portion, PET wartet auf DAV */
  if (!cvalid[sa] || cpos[sa] >= clen[sa])
    return false;
  *b = cbuf(sa)[cpos[sa]];
  *eoi = cpos[sa] + 1 == clen[sa] && ceof[sa];
  return true;
}

void relay_advance(uint8_t sa) {
  cpos[sa]++;
}
