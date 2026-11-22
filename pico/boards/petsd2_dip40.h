/*
 * petSD2: Olimex RP2350-PICO2-XXL auf dem Adapter im DIP40-Sockel des petSD+
 * (Rev. 2.4). Belegung laut docufolder petsd2/adapter-dip40-pinout.md.
 *
 * Vom petSD+ genutzt: 75160/75161, SD-Schacht (74HC4050/74HCT125), ATtiny25
 * (I2C: LCD-PWM und Busfreigabe IO_IEC), DS3231, LCD 20x4, Tasten an einer
 * Widerstandsleiter, LEDs dirty/busy. Neu auf dem Adapter: IEC-Host
 * (74LVC07A Ausgaenge, 74LVC14A Eingaenge). REN und SRQ sind beim petSD+ nicht angeschlossen.
 *
 * Belegung nach der Geometrie der Platine (Olimex um 1,27 mm gegen H1
 * versetzt): jede Leitung geht zum GPIO, der neben ihrem H1-Pin liegt.
 * Regeln: 5-V-Leitungen (7516x, I2C, SD-MISO, CD/WP) nur auf GPIO 0-39,
 * die ADC-Pins 40-47 nur fuer die Tasten und den IEC-Host (3,3 V).
 * D1-D8 der Reihe nach, alle PIO-Leitungen im Fenster 16-47.
 */
// pico_cmake_set PICO_PLATFORM=rp2350

#ifndef _BOARDS_PETSD2_DIP40_H
#define _BOARDS_PETSD2_DIP40_H

#include "olimex_rp2350_pico2_xxl.h"

#define PSD2_BOARD_DIP40

/* IEEE-488 (75160/75161) */
#define PSD2_D_BASE   32      /* D1-D8 = H1.40-34, H1.21 */
#define PSD2_NRFD     24      /* H1.29 */
#define PSD2_NDAC     25      /* H1.28 (Olimex-LED zeigt NDAC) */
#define PSD2_DAV      26      /* H1.27 */
#define PSD2_EOI      27      /* H1.26 */
#define PSD2_ATN      28      /* H1.16 */
#define PSD2_TE       29      /* H1.25 */
#define PSD2_IFC      12      /* H1.24 */
#define PSD2_REN      (-1)    /* nicht angeschlossen */
#define PSD2_SRQ      (-1)    /* nicht angeschlossen */

/* LEDs wie beim petSD+: dirty rot, busy gruen, beide aktiv high */
#define PSD2_LED_DIRTY 10     /* H1.14 */
#define PSD2_LED_BUSY  11     /* H1.15 */

/* SD-Schacht des petSD+ an SPI0 */
#define PSD2_SD_CS    3       /* H1.5 */
#define PSD2_SD_MOSI  19      /* H1.6 */
#define PSD2_SD_MISO  20      /* H1.7 */
#define PSD2_SD_CLK   22      /* H1.8 */
#define PSD2_SD_CD    15      /* H1.19, low = Karte steckt */
#define PSD2_SD_WP    9       /* H1.20, high = schreibgeschuetzt */

/* I2C1: ATtiny25 (0x4C) und DS3231, Pull-ups auf 5 V auf dem petSD+ */
#define PSD2_I2C_SDA  30      /* H1.23 */
#define PSD2_I2C_SCL  31      /* H1.22 */

/* LCD 20x4 (HD44780), 4 Bit, nur schreiben (R/W fest auf GND) */
#define PSD2_LCD_DB4  16      /* H1.1 */
#define PSD2_LCD_DB5  17      /* H1.2 */
#define PSD2_LCD_DB6  18      /* H1.3 */
#define PSD2_LCD_DB7  2       /* H1.4 */
#define PSD2_LCD_RS   13      /* H1.17 */
#define PSD2_LCD_E    14      /* H1.18 */

/* Tasten: Widerstandsleiter an 5 V (H1.33), Teiler 56k/100k auf dem Adapter */
#define PSD2_BUTTONS_ADC 40   /* ADC0 */

/* IEC-Host auf dem Adapter (74LVC07A/74LVC14A an 3,3 V) */
#define IEC_CLK_O   41
#define IEC_DATA_O  42        /* direkt hinter CLK_O (PIO) */
#define IEC_ATN_O   43
#define IEC_SRQ_O   44
#define IEC_RESET_O 45
#define IEC_CLK_I   46
#define IEC_DATA_I  47
#define IEC_ATN_I   21
#define IEC_SRQ_I   23

/* J1 (frei, fuer die petSD2-Platine ohne ATtiny): 4 PWM2 A (Kontrast),
   5 PWM2 B (Helligkeit), 6 Busfreigabe ~IEC, 7 frei; 0 und 1 frei */

#endif
