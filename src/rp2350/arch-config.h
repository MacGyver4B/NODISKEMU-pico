/* arch-config.h - NODISKEMU auf petSD2+IEC (RP2350B, Olimex PICO2-XXL)

   Belegung laut Konzept (18.11.):
     16-23  IEEE D1-D8 (74FCT621A #1), 26 DIR (high = senden)
     27 ATN_I  28 IFC_I  29 REN_I  30 EOI_I  31 DAV_I  32 NRFD_I  33 NDAC_I
     34 EOI_O  35 DAV_O  36 NRFD_O  37 NDAC_O  38 SRQ_O
     9/10/11/24 microSD (CS, CLK, CMD, DAT0), 25 LED

   Die 621 sind Open Drain und nicht invertierend: ein Ausgangspin auf
   low zieht die Busleitung, high gibt sie frei. Eingaenge zeigen den
   Buspegel direkt. Ein- und Ausgaenge sind getrennt, deshalb gibt es
   keine Richtungsumschaltung fuer die Steuerleitungen (TE/DC des
   75160/75161 entfallen); nur die Datenleitungen haben DIR.

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
#define PSD2_DIR      26
#define PSD2_ATN_I    27
#define PSD2_IFC_I    28
#define PSD2_REN_I    29
#define PSD2_EOI_I    30
#define PSD2_DAV_I    31
#define PSD2_NRFD_I   32
#define PSD2_NDAC_I   33
#define PSD2_EOI_O    34
#define PSD2_DAV_O    35
#define PSD2_NRFD_O   36
#define PSD2_NDAC_O   37
#define PSD2_SRQ_O    38
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

static inline uint8_t ieee488_ATN(void)  { return (psd2_in_lo() & LO(PSD2_ATN_I))  != 0; }
static inline uint8_t ieee488_IFC(void)  { return (psd2_in_lo() & LO(PSD2_IFC_I))  != 0; }
static inline uint8_t ieee488_EOI(void)  { return (psd2_in_lo() & LO(PSD2_EOI_I))  != 0; }
static inline uint8_t ieee488_DAV(void)  { return (psd2_in_lo() & LO(PSD2_DAV_I))  != 0; }
static inline uint8_t ieee488_NRFD(void) { return (psd2_in_hi() & HI(PSD2_NRFD_I)) != 0; }
static inline uint8_t ieee488_NDAC(void) { return (psd2_in_hi() & HI(PSD2_NDAC_I)) != 0; }

static inline void psd2_out(uint32_t hibit, bool x) {
  if (x) sio_hw->gpio_hi_set = hibit;     /* Leitung frei */
  else   sio_hw->gpio_hi_clr = hibit;     /* Leitung ziehen */
}

static inline void ieee488_SetEOI(bool x)  { psd2_out(HI(PSD2_EOI_O), x); }
static inline void ieee488_SetDAV(bool x)  { psd2_out(HI(PSD2_DAV_O), x); }
static inline void ieee488_SetNRFD(bool x) { psd2_out(HI(PSD2_NRFD_O), x); }
static inline void ieee488_SetNDAC(bool x) { psd2_out(HI(PSD2_NDAC_O), x); }

/* Daten: Bus low = logisch 1 (wie beim AVR invertiert) */
static inline uint8_t ieee488_Data(void) {
  return (uint8_t)~(psd2_in_lo() >> PSD2_D_BASE);
}

static inline void ieee488_SetData(uint8_t data) {
  sio_hw->gpio_out = (sio_hw->gpio_out & ~PSD2_D_MASK) |
                     ((uint32_t)(uint8_t)~data << PSD2_D_BASE);
}

static inline void ieee488_DataListen(void) {
  sio_hw->gpio_oe_clr = PSD2_D_MASK;      /* erst loslassen ... */
  sio_hw->gpio_clr = LO(PSD2_DIR);        /* ... dann 621 auf Empfang */
  ieee488_TE75160 = 0;
}

static inline void ieee488_DataTalk(void) {
  sio_hw->gpio_set = PSD2_D_MASK;         /* alle Leitungen frei */
  sio_hw->gpio_oe_set = PSD2_D_MASK;
  sio_hw->gpio_set = LO(PSD2_DIR);        /* 621 auf Senden */
  ieee488_TE75160 = 1;
}

/* Steuerleitungen: Open Drain, nur die jeweils fremden loslassen */
static inline void ieee488_CtrlPortsListen(void) {
  ieee488_SetDAV(1);
  ieee488_SetEOI(1);
  ieee488_TE75161 = 0;
}

static inline void ieee488_CtrlPortsTalk(void) {
  ieee488_SetNDAC(1);
  ieee488_SetNRFD(1);
  ieee488_SetDAV(1);
  ieee488_SetEOI(1);
  ieee488_TE75161 = 1;
}

static inline void ieee488_InitIFC(void) {}
static inline void ieee488_InitDC(void) {}
static inline void ieee488_SetDC(bool x) { (void)x; }
static inline void ieee488_SetTE(bool x) { (void)x; }

/* ATN: PIO-Zustandsmaschine meldet die fallende Flanke per IRQ,
   die Quittung (NRFD ziehen, NDAC frei) macht der ISR (ieee-ll.c) */
void ieee488_InitAtnInterrupt(void);
void ieee488_EnableAtnInterrupt(void);
void ieee488_DisableAtnInterrupt(void);

#endif
