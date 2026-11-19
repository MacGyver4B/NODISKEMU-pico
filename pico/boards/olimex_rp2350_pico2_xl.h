/*
 * Olimex RP2350-PICO2-XL (RP2350B, 48 GPIO, 2 MB Flash)
 * Eigene Board-Datei fuer petSD2+IEC. Gleiche Platine wie das XXL, aber ohne
 * PSRAM und ohne microSD; der SD-Steckplatz sitzt auf der Traegerplatine, an
 * GPIO 9/10/11/24 (SPI1). Board: GPIO 25 LED.
 */
#ifndef _BOARDS_OLIMEX_RP2350_PICO2_XL_H
#define _BOARDS_OLIMEX_RP2350_PICO2_XL_H

// pico_cmake_set PICO_PLATFORM=rp2350

#define OLIMEX_RP2350_PICO2_XL

// RP2350B: 48 GPIO
#define PICO_RP2350A 0
#define PICO_RP2350_A2_SUPPORTED 1

#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25
#endif

#ifndef PICO_DEFAULT_I2C
#define PICO_DEFAULT_I2C 1
#endif
#ifndef PICO_DEFAULT_I2C_SDA_PIN
#define PICO_DEFAULT_I2C_SDA_PIN 2
#endif
#ifndef PICO_DEFAULT_I2C_SCL_PIN
#define PICO_DEFAULT_I2C_SCL_PIN 3
#endif

// petSD2+IEC: IEEE-, IEC- und SD-Pins kommen aus den Vorgaben in
// src/rp2350/arch-config.h (PSD2_*) und src/rp2350/iec-host.h (IEC_*), sie
// passen zu diesem Board (SD-Steckplatz 9/10/11/24 an SPI1, LED 25). Ein Board
// mit anderer Belegung definiert die abweichenden Pins hier.

#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif
// 2 MB
// pico_cmake_set_default PICO_FLASH_SIZE_BYTES = (2 * 1024 * 1024)
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

// pico_cmake_set_default PICO_RP2350_A2_SUPPORTED = 1

#endif
