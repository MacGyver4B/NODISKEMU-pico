/*
 * petSD2: Olimex RP2350-PICO2-XXL auf dem Adapter im DIP40-Sockel des petSD+
 * (Rev. 2.4). Belegung laut docufolder petsd2/adapter-dip40-pinout.md.
 *
 * Vom petSD+ genutzt: 75160/75161, SD-Schacht (74HC4050/74HCT125), ATtiny25
 * (I2C: LCD-PWM und Busfreigabe IO_IEC), DS3231, LCD 20x4, Tasten an ADC7,
 * LEDs dirty/busy. Neu auf dem Adapter: IEC-Host (74LVC07A, 74LVC14A).
 * REN und SRQ sind beim petSD+ nicht angeschlossen.
 */
// pico_cmake_set PICO_PLATFORM=rp2350

#ifndef _BOARDS_PETSD2_DIP40_H
#define _BOARDS_PETSD2_DIP40_H

#include "olimex_rp2350_pico2_xxl.h"

#define PSD2_BOARD_DIP40

/* IEEE-488 (75160/75161) */
#define PSD2_D_BASE   16      /* D1-D8 */
#define PSD2_TE       26
#define PSD2_ATN      27
#define PSD2_IFC      28
#define PSD2_REN      (-1)    /* nicht angeschlossen */
#define PSD2_EOI      30
#define PSD2_DAV      31
#define PSD2_NRFD     32
#define PSD2_NDAC     33
#define PSD2_SRQ      (-1)    /* nicht angeschlossen */

/* LEDs wie beim petSD+: dirty rot, busy gruen, beide aktiv high */
#define PSD2_LED_DIRTY 4
#define PSD2_LED_BUSY  5

/* SD-Schacht des petSD+ an SPI1 */
#define PSD2_SD_MISO  12
#define PSD2_SD_CS    13
#define PSD2_SD_CLK   14
#define PSD2_SD_MOSI  15
#define PSD2_SD_CD    2       /* low = Karte steckt */
#define PSD2_SD_WP    3       /* high = schreibgeschuetzt */

/* I2C1: ATtiny25 (0x4C) und DS3231, Pull-ups auf 5 V auf dem petSD+ */
#define PSD2_I2C_SDA  6
#define PSD2_I2C_SCL  7

/* LCD 20x4 (HD44780), 4 Bit, nur schreiben (R/W fest auf GND) */
#define PSD2_LCD_DB4  25
#define PSD2_LCD_DB5  29
#define PSD2_LCD_DB6  34
#define PSD2_LCD_DB7  35
#define PSD2_LCD_RS   36
#define PSD2_LCD_E    37

/* Tasten: Widerstandsleiter an 5 V, Teiler 56k/100k auf dem Adapter */
#define PSD2_BUTTONS_ADC 47   /* ADC7 */

/* IEC-Host auf dem Adapter */
#define IEC_CLK_I   43
#define IEC_DATA_I  44
#define IEC_ATN_I   45
#define IEC_SRQ_I   46
#define IEC_CLK_O   38
#define IEC_DATA_O  39
#define IEC_ATN_O   40
#define IEC_SRQ_O   41
#define IEC_RESET_O 42

#endif
