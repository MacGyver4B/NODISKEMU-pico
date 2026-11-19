/* arch-config.h - NODISKEMU auf petSD2+IEC (RP2350B, Olimex PICO2-XXL)

   IEEE-488 wie beim petSD+ ueber SN75160 (Daten) und SN75161 (Steuerung),
   DC fest auf high (Geraet), ein gemeinsames TE fuer beide Bausteine.
   Alle 7516x-Leitungen liegen auf 5-V-toleranten GPIOs (< 40, RP2350 A4):
     16-23  D1-D8 (75160)
     26     TE (high = sprechen)
     27 ATN  28 IFC  29 REN            immer Eingang
     30 EOI  31 DAV                    Ausgang beim Sprechen, sonst Eingang
     32 NRFD 33 NDAC                   Ausgang beim Hoeren, sonst Eingang
     34 SRQ                            Ausgang (frei = high)
     9/10/11/24 microSD (CS, CLK, CMD, DAT0), 25 LED

   Pegel wie am Bus: Pin low = Leitung low. Beim Umschalten von TE gilt die
   Reihenfolge des petSD+ (ieee.c), damit MCU und Treiber eine Leitung nie
   gleichzeitig treiben.

   Alle Zugriffe gehen direkt auf die SIO-Register, das ist schnell und
   laeuft auch in picoem (dort ohne GPIO-Coprozessor). */

#ifndef ARCH_CONFIG_H
#define ARCH_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/structs/sio.h"

/* ----- Allgemein ----- */

/* hardware_uart des SDK hat ein eigenes uart_init */
#define uart_init nde_uart_init

typedef unsigned int rawbutton_t;

#define HAVE_SD
#define SD_SUPPLY_VOLTAGE (1L << 21)        /* 3.3 V */

/* EEPROMFS: Werte nur, damit eeprom-fs.h uebersetzt */
#define EEPROMFS_OFFSET     512
#define EEPROMFS_SIZE       3584
#define EEPROMFS_ENTRIES    8
#define EEPROMFS_SECTORSIZE 64

/* ----- Pins ----- */

#define PSD2_D_BASE   16
#define PSD2_TE       26
#define PSD2_ATN      27
#define PSD2_IFC      28
#define PSD2_REN      29
#define PSD2_EOI      30
#define PSD2_DAV      31
#define PSD2_NRFD     32
#define PSD2_NDAC     33
#define PSD2_SRQ      34
#define PSD2_LED      25

#define PSD2_D_MASK   (0xffu << PSD2_D_BASE)
#define HI(p)         (1u << ((p) - 32))
#define LO(p)         (1u << (p))

static inline uint32_t psd2_in_lo(void) { return sio_hw->gpio_in; }
static inline uint32_t psd2_in_hi(void) { return sio_hw->gpio_hi_in; }

/* ----- SD-Karte ----- */

void psd2_board_init(void);

static inline void sdcard_interface_init(void) {}
static inline void sdcard_set_ss(int state) { (void)state; }   /* ueber spi_select_device */
static inline uint8_t sdcard_detect(void) { return 1; }        /* kein Kartenschalter */
static inline uint8_t sdcard_wp(void) { return 0; }

/* ----- Geraeteadresse, Tasten ----- */

static inline void device_hw_address_init(void) {}
static inline uint8_t device_hw_address(void) { return CONFIG_DEFAULT_ADDR; }

static inline void buttons_init(void) {}
static inline rawbutton_t buttons_read(void) { return 0; }

/* ----- LEDs: eine LED (GPIO 25) fuer busy und dirty ----- */

static inline void leds_init(void) {
  sio_hw->gpio_clr = LO(PSD2_LED);
  sio_hw->gpio_oe_set = LO(PSD2_LED);
}

static inline __attribute__((always_inline)) void set_busy_led(uint8_t state) {
  if (state) sio_hw->gpio_set = LO(PSD2_LED);
  else       sio_hw->gpio_clr = LO(PSD2_LED);
}

static inline __attribute__((always_inline)) void set_dirty_led(uint8_t state) {
  if (state) sio_hw->gpio_set = LO(PSD2_LED);
  else       sio_hw->gpio_clr = LO(PSD2_LED);
}

static inline void toggle_dirty_led(void) {
  sio_hw->gpio_togl = LO(PSD2_LED);
}

#define HAVE_EARLY_BOARD_INIT
static inline void early_board_init(void) {
  psd2_board_init();
}

/* ----- IEEE-488 ----- */

#define HAVE_ARCH_IEEE_LL       /* ersetzt die AVR-Registerzugriffe in ieee.c */

extern volatile bool ieee488_TE75160;
extern volatile bool ieee488_TE75161;
extern volatile bool ieee488_ATN_received;

static inline void ieee_interface_init(void) {}

static inline uint8_t ieee488_ATN(void)  { return (psd2_in_lo() & LO(PSD2_ATN))  != 0; }
static inline uint8_t ieee488_IFC(void)  { return (psd2_in_lo() & LO(PSD2_IFC))  != 0; }
static inline uint8_t ieee488_EOI(void)  { return (psd2_in_lo() & LO(PSD2_EOI))  != 0; }
static inline uint8_t ieee488_DAV(void)  { return (psd2_in_lo() & LO(PSD2_DAV))  != 0; }
static inline uint8_t ieee488_NRFD(void) { return (psd2_in_hi() & HI(PSD2_NRFD)) != 0; }
static inline uint8_t ieee488_NDAC(void) { return (psd2_in_hi() & HI(PSD2_NDAC)) != 0; }

/* Ausgangswert setzen; ob er am Bus ankommt, entscheidet die Richtung */
static inline void ieee488_SetEOI(bool x) {
  if (x) sio_hw->gpio_set = LO(PSD2_EOI); else sio_hw->gpio_clr = LO(PSD2_EOI);
}
static inline void ieee488_SetDAV(bool x) {
  if (x) sio_hw->gpio_set = LO(PSD2_DAV); else sio_hw->gpio_clr = LO(PSD2_DAV);
}
static inline void ieee488_SetNRFD(bool x) {
  if (x) sio_hw->gpio_hi_set = HI(PSD2_NRFD); else sio_hw->gpio_hi_clr = HI(PSD2_NRFD);
}
static inline void ieee488_SetNDAC(bool x) {
  if (x) sio_hw->gpio_hi_set = HI(PSD2_NDAC); else sio_hw->gpio_hi_clr = HI(PSD2_NDAC);
}

/* Daten: Bus low = logisch 1 (wie beim AVR invertiert) */
static inline uint8_t ieee488_Data(void) {
  return (uint8_t)~(psd2_in_lo() >> PSD2_D_BASE);
}

static inline void ieee488_SetData(uint8_t data) {
  sio_hw->gpio_out = (sio_hw->gpio_out & ~PSD2_D_MASK) |
                     ((uint32_t)(uint8_t)~data << PSD2_D_BASE);
}

static inline void ieee488_SetTE(bool x) {
  if (x) sio_hw->gpio_set = LO(PSD2_TE); else sio_hw->gpio_clr = LO(PSD2_TE);
}

/* Datenleitungen: Richtung der GPIOs; TE schaltet CtrlPorts* (gemeinsam) */
static inline void ieee488_DataListen(void) {
  sio_hw->gpio_oe_clr = PSD2_D_MASK;      /* Daten als Eingang */
  ieee488_TE75160 = 0;
}

static inline void ieee488_DataTalk(void) {
  sio_hw->gpio_set = PSD2_D_MASK;         /* alle Leitungen frei */
  sio_hw->gpio_oe_set = PSD2_D_MASK;      /* Daten als Ausgang */
  ieee488_TE75160 = 1;
}

/* Reihenfolge wie ieee.c beim petSD+ (HAVE_7516X) */
static inline void ieee488_CtrlPortsListen(void) {
  sio_hw->gpio_oe_clr = LO(PSD2_EOI) | LO(PSD2_DAV);       /* EOI, DAV Eingang */
  ieee488_SetTE(0);
  sio_hw->gpio_hi_oe_set = HI(PSD2_NDAC) | HI(PSD2_NRFD);  /* NDAC, NRFD Ausgang */
  ieee488_TE75161 = 0;
}

static inline void ieee488_CtrlPortsTalk(void) {
  sio_hw->gpio_hi_oe_clr = HI(PSD2_NDAC) | HI(PSD2_NRFD);  /* NDAC, NRFD Eingang */
  ieee488_SetDAV(1);
  ieee488_SetEOI(1);
  ieee488_SetTE(1);
  sio_hw->gpio_oe_set = LO(PSD2_EOI) | LO(PSD2_DAV);       /* EOI, DAV Ausgang */
  ieee488_TE75161 = 1;
}

static inline void ieee488_InitIFC(void) {}
static inline void ieee488_InitDC(void) {}           /* DC fest high */
static inline void ieee488_SetDC(bool x) { (void)x; }

/* ATN: PIO-Zustandsmaschine meldet die fallende Flanke per IRQ,
   die Quittung (NRFD ziehen, NDAC frei) macht der ISR (ieee-ll.c) */
void ieee488_InitAtnInterrupt(void);
void ieee488_EnableAtnInterrupt(void);
void ieee488_DisableAtnInterrupt(void);

#endif
