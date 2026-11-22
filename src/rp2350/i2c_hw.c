/* i2c_hw.c - Hardware-I2C1 fuer petSD2 (RP2350), ohne config.h, weil
   NODISKEMU ein eigenes i2c_init() hat (wie uart_hw.c) */
#include "hardware/i2c.h"
#include "hardware/gpio.h"

#define I2C_BUS      i2c1
#define I2C_TIMEOUT  5000          /* us pro Uebertragung */

void hwi2c_init(unsigned sda, unsigned scl, unsigned hz) {
  i2c_init(I2C_BUS, hz);
  gpio_set_function(sda, GPIO_FUNC_I2C);
  gpio_set_function(scl, GPIO_FUNC_I2C);
  gpio_disable_pulls(sda);         /* Pull-ups auf dem petSD+ an 5 V */
  gpio_disable_pulls(scl);
}

/* Rueckgabe: Anzahl Bytes oder PICO_ERROR_* (< 0) */
int hwi2c_write(unsigned addr7, const unsigned char *buf, unsigned len, int nostop) {
  return i2c_write_timeout_us(I2C_BUS, addr7, buf, len, nostop, I2C_TIMEOUT);
}

int hwi2c_read(unsigned addr7, unsigned char *buf, unsigned len, int nostop) {
  return i2c_read_timeout_us(I2C_BUS, addr7, buf, len, nostop, I2C_TIMEOUT);
}

int hwi2c_err_timeout(void) { return PICO_ERROR_TIMEOUT; }
