/* arch-config.h - NODISKEMU auf petSD2+IEC (RP2350B)

   IEEE-488 wie beim petSD+ ueber SN75160 (Daten) und SN75161 (Steuerung),
   DC fest auf high (Geraet), ein gemeinsames TE fuer beide Bausteine.
   Vorgabe-Belegung (Olimex PICO2-XXL und PICO2-BB48); ein Board kann jeden
   Pin in seiner Datei unter pico/boards/ ueberschreiben, die Pruefungen
   unten stellen sicher, dass die Belegung zum Code passt.
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

/* ----- Pins (Vorgabe, ueberschreibbar in pico/boards/<board>.h) ----- */

#ifndef PSD2_D_BASE
#define PSD2_D_BASE   16      /* D1-D8 zusammenhaengend ab hier */
#endif
#ifndef PSD2_TE
#define PSD2_TE       26
#endif
#ifndef PSD2_ATN
#define PSD2_ATN      27
#endif
#ifndef PSD2_IFC
#define PSD2_IFC      28
#endif
#ifndef PSD2_REN
#define PSD2_REN      29
#endif
#ifndef PSD2_EOI
#define PSD2_EOI      30
#endif
#ifndef PSD2_DAV
#define PSD2_DAV      31
#endif
#ifndef PSD2_NRFD
#define PSD2_NRFD     32
#endif
#ifndef PSD2_NDAC
#define PSD2_NDAC     33
#endif
#ifndef PSD2_SRQ
#define PSD2_SRQ      34
#endif
#ifndef PSD2_LED
#define PSD2_LED      25
#endif
/* busy und dirty: Vorgabe eine gemeinsame LED */
#ifndef PSD2_LED_BUSY
#define PSD2_LED_BUSY  PSD2_LED
#endif
#ifndef PSD2_LED_DIRTY
#define PSD2_LED_DIRTY PSD2_LED
#endif
/* Kartenschalter (low = Karte steckt) und Schreibschutz (high = geschuetzt);
   -1 = nicht vorhanden */
#ifndef PSD2_SD_CD
#define PSD2_SD_CD    (-1)
#endif
#ifndef PSD2_SD_WP
#define PSD2_SD_WP    (-1)
#endif
/* microSD an SPI1 */
#ifndef PSD2_SD_CS
#define PSD2_SD_CS    9
#endif
#ifndef PSD2_SD_CLK
#define PSD2_SD_CLK   10
#endif
#ifndef PSD2_SD_MOSI
#define PSD2_SD_MOSI  11
#endif
#ifndef PSD2_SD_MISO
#define PSD2_SD_MISO  24
#endif
/* Fenster der PIO-Bloecke (ATN-Erkennung, IEC-Senden): GPIO base..base+31 */
#ifndef PSD2_PIO_GPIO_BASE
#define PSD2_PIO_GPIO_BASE 16
#endif

/* Pruefungen: der Code liest D1-D8, ATN, IFC, EOI, DAV, TE und die LED ueber
   die unteren SIO-Register (GPIO 0-31), NRFD und NDAC ueber die oberen
   (32-47). Die 7516x liefern 5 V, die ADC-Pins 40-47 vertragen das nicht. */
#if PSD2_D_BASE < 0 || PSD2_D_BASE + 7 > 31
#  error "PSD2_D_BASE: D1-D8 muessen zusammenhaengend in GPIO 0-31 liegen"
#endif
#if PSD2_TE > 31 || PSD2_ATN > 31 || PSD2_IFC > 31 || PSD2_REN > 31 || \
    PSD2_EOI > 31 || PSD2_DAV > 31 || PSD2_LED_BUSY > 31 || PSD2_LED_DIRTY > 31
#  error "TE, ATN, IFC, REN, EOI, DAV und LEDs muessen in GPIO 0-31 liegen"
#endif
#if PSD2_SD_CD > 31 || PSD2_SD_WP > 31
#  error "PSD2_SD_CD und PSD2_SD_WP muessen in GPIO 0-31 liegen"
#endif
#if PSD2_NRFD < 32 || PSD2_NRFD > 39 || PSD2_NDAC < 32 || PSD2_NDAC > 39
#  error "NRFD und NDAC muessen in GPIO 32-39 liegen (oberes SIO-Register, unter den ADC-Pins)"
#endif
#if PSD2_SRQ > 39
#  error "SRQ muss unter GPIO 40 liegen (5-V-Toleranz)"
#endif
#if PSD2_PIO_GPIO_BASE != 0 && PSD2_PIO_GPIO_BASE != 16
#  error "PSD2_PIO_GPIO_BASE muss 0 oder 16 sein"
#endif
#if PSD2_ATN < PSD2_PIO_GPIO_BASE || PSD2_ATN > PSD2_PIO_GPIO_BASE + 31
#  error "ATN muss im PIO-Fenster liegen (PSD2_PIO_GPIO_BASE..+31)"
#endif
#if PSD2_SD_CS == PSD2_SD_CLK || PSD2_SD_CS == PSD2_SD_MOSI || PSD2_SD_CS == PSD2_SD_MISO
#  error "PSD2_SD_CS darf keinen SPI-Pin doppelt belegen"
#endif
#if (PSD2_SD_CLK % 4) != 2 || (PSD2_SD_MOSI % 4) != 3 || (PSD2_SD_MISO % 4) != 0 || \
    ((PSD2_SD_CLK / 8) % 2) != 1 || ((PSD2_SD_MOSI / 8) % 2) != 1 || ((PSD2_SD_MISO / 8) % 2) != 1
#  error "PSD2_SD_CLK/MOSI/MISO muessen SPI1-Pins sein (SCK, TX, RX)"
#endif

#define PSD2_D_MASK   (0xffu << PSD2_D_BASE)
#define HI(p)         (1u << ((p) - 32))
#define LO(p)         (1u << (p))

static inline uint32_t psd2_in_lo(void) { return sio_hw->gpio_in; }
static inline uint32_t psd2_in_hi(void) { return sio_hw->gpio_hi_in; }

/* ----- SD-Karte ----- */

void psd2_board_init(void);

static inline void sdcard_interface_init(void) {}
static inline void sdcard_set_ss(int state) { (void)state; }   /* ueber spi_select_device */
static inline uint8_t sdcard_detect(void) {
#if PSD2_SD_CD >= 0 && !defined(CONFIG_IGNORE_CARD_DETECT)
  return !(psd2_in_lo() & LO(PSD2_SD_CD));
#else
  return 1;                                                   /* kein Kartenschalter */
#endif
}
static inline uint8_t sdcard_wp(void) {
#if PSD2_SD_WP >= 0
  return (psd2_in_lo() & LO(PSD2_SD_WP)) != 0;
#else
  return 0;
#endif
}

/* ----- LEDs: busy und dirty (Vorgabe: eine gemeinsame LED) ----- */

static inline void leds_init(void) {
  sio_hw->gpio_clr = LO(PSD2_LED_BUSY) | LO(PSD2_LED_DIRTY);
  sio_hw->gpio_oe_set = LO(PSD2_LED_BUSY) | LO(PSD2_LED_DIRTY);
}

static inline __attribute__((always_inline)) void set_busy_led(uint8_t state) {
  if (state) sio_hw->gpio_set = LO(PSD2_LED_BUSY);
  else       sio_hw->gpio_clr = LO(PSD2_LED_BUSY);
}

static inline __attribute__((always_inline)) void set_dirty_led(uint8_t state) {
  if (state) sio_hw->gpio_set = LO(PSD2_LED_DIRTY);
  else       sio_hw->gpio_clr = LO(PSD2_LED_DIRTY);
}

static inline void toggle_dirty_led(void) {
  sio_hw->gpio_togl = LO(PSD2_LED_DIRTY);
}

/* ----- Bedienung wie beim petSD+ (LCD, Tasten, ATtiny25, Uhr) -----
   Jede Funktion ist einzeln abschaltbar: CONFIG_ONBOARD_DISPLAY (LCD, Menue,
   Tasten), CONFIG_RTC_DSRTC (DS3231). I2C ist da, sobald das Board Pins dafuer
   hat; dann wird auch der ATtiny25 (Busfreigabe, LCD-PWM) angesprochen. */

#ifdef PSD2_I2C_SDA
#  define HAVE_I2C
#  define i2c_init nde_i2c_init   /* hardware_i2c des SDK hat ein eigenes i2c_init */
#  define PSD2_HAVE_ATTINY      /* ATtiny25 des petSD+ 2.x */
#  include "i2c-lcd-pwm-io-config.h"
#endif

#ifdef CONFIG_ONBOARD_DISPLAY
#  if !defined(PSD2_LCD_E) || !defined(PSD2_BUTTONS_ADC)
#    error "CONFIG_ONBOARD_DISPLAY braucht ein Board mit LCD und Tasten (z. B. petsd2_dip40)"
#  endif
#  if !defined(PSD2_HAVE_ATTINY)
#    error "CONFIG_ONBOARD_DISPLAY braucht den ATtiny25 (Helligkeit/Kontrast) und damit I2C"
#  endif
#  define HAVE_ANALOG_BUTTONS
#  define LCD_LINES             4
#  define LCD_COLS              20
#  define LCD_ADDR_LINE1        0
#  define LCD_ADDR_LINE2        64
#  define LCD_ADDR_LINE3        20
#  define LCD_ADDR_LINE4        84
#  define HAVE_EARLY_LCD_INIT
#  define HAVE_LATE_BOARD_INIT
#endif
#if defined(CONFIG_RTC_DSRTC) && !defined(HAVE_I2C)
#  error "CONFIG_RTC_DSRTC braucht ein Board mit I2C"
#endif

#define HAVE_EARLY_BOARD_INIT

#ifdef CONFIG_ONBOARD_DISPLAY
#include "analog-buttons.h"
#include "eeprom-conf.h"
#include "menu.h"
#include "timer.h"
#include "lcd.h"
#include "diagnose.h"
#include <stdio.h>

static inline void buttons_init(void) { analog_buttons_init(); }

static inline void device_hw_address_init(void) {}

/* wie beim petSD+: ohne Menue waehlen SEL/NEXT die Adresse 8-11 */
static inline uint8_t device_hw_address(void) {
  uint8_t addr;
  if (menu_system_enabled) {
    addr = CONFIG_DEFAULT_ADDR;
  } else {
    delay_ms(500);
    addr = 8;
    if (get_key_state(KEY_SEL))  addr += 1;
    if (get_key_state(KEY_NEXT)) addr += 2;
#ifdef CONFIG_UART_DEBUG
    printf("dhw:%d\r\n", addr);
#endif
  }
  return addr;
}

static inline void early_board_init(void) {
  psd2_board_init();
  timer_init();                 /* delay_us fuer das LCD (main ruft es spaeter erneut) */
  /* Startbild, bevor eine Initialisierung haengen kann (wie petSD+) */
  lcd_init();
  lcd_bootscreen();
}

static inline void late_board_init(void) {
  if (!menu_system_enabled) return;
  uint8_t buttons = analog_buttons_boot_keys();
  (void)get_key_press(KEY_ANY);
  (void)get_key_rpt(KEY_ANY);
  if (buttons == KEY_PREV) {
    board_diagnose();
  } else if (buttons == KEY_NEXT) {
    lcd_set_brightness(0xFF);
    menu_adjust_contrast();
  }
}
#else
static inline void buttons_init(void) {}
static inline rawbutton_t buttons_read(void) { return 0; }
static inline void device_hw_address_init(void) {}
static inline uint8_t device_hw_address(void) { return CONFIG_DEFAULT_ADDR; }
static inline void early_board_init(void) {
  psd2_board_init();
}
#endif

/* ----- IEEE-488 ----- */

#define HAVE_ARCH_IEEE_LL       /* ersetzt die AVR-Registerzugriffe in ieee.c */

extern volatile bool ieee488_TE75160;
extern volatile bool ieee488_TE75161;
extern volatile bool ieee488_ATN_received;

#ifdef PSD2_HAVE_ATTINY
#include "i2c.h"
/* Der ATtiny25 schaltet die 7516x erst frei, wenn IO_IEC gesetzt ist */
static inline void ieee_interface_init(void) {
  i2c_write_register(I2C_SLAVE_ADDRESS, IO_IEC, 1);
}
#else
static inline void ieee_interface_init(void) {}
#endif

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
