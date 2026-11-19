/*
 * Olimex RP2350-PICO2-XXL (RP2350B, 48 GPIO, 16 MB Flash, 8 MB PSRAM, microSD)
 * Eigene Board-Datei fuer petSD2+IEC; Belegung des Boards laut Olimex-Pinout:
 * GPIO 8 PSRAM-CS, 9/10/11/24 microSD (CS, CLK, CMD, DAT0), 25 LED.
 */
#ifndef _BOARDS_OLIMEX_PICO2_XXL_H
#define _BOARDS_OLIMEX_PICO2_XXL_H

// pico_cmake_set PICO_PLATFORM=rp2350

#define OLIMEX_PICO2_XXL

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

// microSD auf dem Board
#define OLIMEX_SD_CS_PIN   9
#define OLIMEX_SD_CLK_PIN  10
#define OLIMEX_SD_CMD_PIN  11
#define OLIMEX_SD_DAT0_PIN 24
#define OLIMEX_PSRAM_CS_PIN 8

#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif
// pico_cmake_set_default PICO_FLASH_SIZE_BYTES = (16 * 1024 * 1024)
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

// pico_cmake_set_default PICO_RP2350_A2_SUPPORTED = 1

#endif
