/* spi.c - SPI1 fuer die microSD-Karte

   Belegung aus arch-config.h bzw. der Board-Datei (Vorgabe GPIO 9 CS,
   10 CLK, 11 CMD/MOSI, 24 DAT0/MISO). CS wird von Hand gesetzt. Im
   Emulator (PSD2_EMU) gehen die Bytes ueber das Postfach an das
   SD-Modell der Bruecke. */
#ifndef PSD2_EMU
#  include "hardware/spi.h"   /* vor spi.h: deklariert das SDK-spi_init */
#  include "hardware/gpio.h"
#endif
#include "config.h"
#include "spi.h"
#ifdef PSD2_EMU
#  include "emu.h"
#endif

#define SD_CS   PSD2_SD_CS
#define SD_CLK  PSD2_SD_CLK
#define SD_MOSI PSD2_SD_MOSI
#define SD_MISO PSD2_SD_MISO

#ifdef PSD2_EMU

void nde_spi_init(spi_speed_t speed) { (void)speed; spi_select_device(SPIDEV_NONE); }
void spi_set_speed(spi_speed_t speed) { (void)speed; }

void spi_select_device(spi_device_t dev) {
  EMU_MBOX->arg = (dev & SPIDEV_CARD0) ? 0 : 1;
  emu_call(EMU_SD_SELECT);
}

void spi_tx_byte(uint8_t data) {
  EMU_MBOX->arg = data;
  emu_call(EMU_SD_XFER);
}

uint8_t spi_rx_byte(void) {
  EMU_MBOX->arg = 0xff;
  emu_call(EMU_SD_XFER);
  return (uint8_t)EMU_MBOX->res;
}

void spi_tx_block(const void *data, unsigned int length) {
  const uint8_t *p = data;
  while (length) {
    unsigned int n = length > sizeof EMU_MBOX->buf ? sizeof EMU_MBOX->buf : length;
    for (unsigned int i = 0; i < n; i++)
      EMU_MBOX->buf[i] = p[i];
    EMU_MBOX->len = n;
    emu_call(EMU_SD_TXBLOCK);
    p += n;
    length -= n;
  }
}

void spi_rx_block(void *data, unsigned int length) {
  uint8_t *p = data;
  while (length) {
    unsigned int n = length > sizeof EMU_MBOX->buf ? sizeof EMU_MBOX->buf : length;
    EMU_MBOX->len = n;
    emu_call(EMU_SD_RXBLOCK);
    for (unsigned int i = 0; i < n; i++)
      p[i] = EMU_MBOX->buf[i];
    p += n;
    length -= n;
  }
}

#else

void spi_set_speed(spi_speed_t speed) {
  spi_set_baudrate(spi1, speed == SPI_SPEED_SLOW ? 400000 : 25000000);
}

void nde_spi_init(spi_speed_t speed) {
  (spi_init)(spi1, 400000);   /* SDK-Funktion, nicht das Makro aus spi.h */
  spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
  gpio_set_function(SD_CLK, GPIO_FUNC_SPI);
  gpio_set_function(SD_MOSI, GPIO_FUNC_SPI);
  gpio_set_function(SD_MISO, GPIO_FUNC_SPI);
  gpio_pull_up(SD_MISO);
  gpio_init(SD_CS);
  gpio_put(SD_CS, 1);
  gpio_set_dir(SD_CS, GPIO_OUT);
  spi_set_speed(speed);
}

void spi_select_device(spi_device_t dev) {
  gpio_put(SD_CS, (dev & SPIDEV_CARD0) ? 0 : 1);
}

void spi_tx_byte(uint8_t data) {
  spi_write_blocking(spi1, &data, 1);
}

uint8_t spi_rx_byte(void) {
  uint8_t r;
  spi_read_blocking(spi1, 0xff, &r, 1);
  return r;
}

void spi_tx_block(const void *data, unsigned int length) {
  spi_write_blocking(spi1, data, length);
}

void spi_rx_block(void *data, unsigned int length) {
  spi_read_blocking(spi1, 0xff, data, length);
}

#endif
