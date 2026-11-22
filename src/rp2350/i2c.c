/* i2c.c - I2C fuer petSD2 (RP2350): Hardware-I2C1, API wie softi2c.c

   Adressen wie im restlichen NODISKEMU als 8-Bit-Wert mit R/W-Bit 0
   (ATtiny25 0x4C, DS3231 0xD0). Die Pull-ups sitzen auf dem petSD+ an 5 V,
   die internen bleiben aus. 100 kHz wie der ATtiny25 (USI) es sicher kann. */
#include "config.h"
#include "i2c.h"
#include <string.h>

#ifdef HAVE_I2C

#define I2C_HZ       100000

void hwi2c_init(unsigned sda, unsigned scl, unsigned hz);
int hwi2c_write(unsigned addr7, const unsigned char *buf, unsigned len, int nostop);
int hwi2c_read(unsigned addr7, unsigned char *buf, unsigned len, int nostop);
int hwi2c_err_timeout(void);

#if (PSD2_I2C_SDA % 4) != 2 || (PSD2_I2C_SCL % 4) != 3
#  error "PSD2_I2C_SDA/SCL muessen I2C1-Pins sein (SDA = 4n+2, SCL = 4n+3)"
#endif

static uint8_t status(int rc) {
  if (rc == hwi2c_err_timeout()) return I2C_ERROR_SCL_TIMEOUT;
  if (rc < 0) return I2C_ERROR_ADDRESS_NACK;
  return 0;
}

void i2c_init(void) {
  hwi2c_init(PSD2_I2C_SDA, PSD2_I2C_SCL, I2C_HZ);
}

uint8_t i2c_write_registers(uint8_t address, uint8_t startreg, uint8_t count, const void *data) {
  uint8_t buf[1 + 255];
  buf[0] = startreg;
  memcpy(buf + 1, data, count);
  int rc = hwi2c_write(address >> 1, buf, 1u + count, 0);
  return rc == 1 + count ? 0 : (rc < 0 ? status(rc) : I2C_ERROR_DATA_NACK);
}

uint8_t i2c_write_register(uint8_t address, uint8_t reg, uint8_t val) {
  return i2c_write_registers(address, reg, 1, &val);
}

uint8_t i2c_read_registers(uint8_t address, uint8_t startreg, uint8_t count, void *data) {
  if (!count) return I2C_ERROR_DATA_NACK;
  int rc = hwi2c_write(address >> 1, &startreg, 1, 1);
  if (rc != 1) return rc < 0 ? status(rc) : I2C_ERROR_REGISTER_NACK;
  rc = hwi2c_read(address >> 1, data, count, 0);
  return rc == count ? 0 : (rc < 0 ? status(rc) : I2C_ERROR_DATA_NACK);
}

int16_t i2c_read_register(uint8_t address, uint8_t reg) {
  uint8_t val;
  if (i2c_read_registers(address, reg, 1, &val)) return -1;
  return val;
}

/* Bloecke zu einem Puffer zusammensetzen (NODISKEMU nutzt sie nur fuer
   die Fernanzeige, die der petSD2 nicht hat) */
static unsigned gather(i2cblock_t *b, unsigned n, uint8_t *buf, unsigned max) {
  unsigned len = 0;
  for (; b && n; b = b->next, n--) {
    if (len + b->length > max) return 0;
    memcpy(buf + len, b->data, b->length);
    len += b->length;
  }
  return len;
}

uint8_t i2c_write_blocks(uint8_t address, i2cblock_t *head) {
  uint8_t buf[256];
  unsigned len = gather(head, ~0u, buf, sizeof(buf));
  if (!len) return I2C_ERROR_DATA_NACK;
  int rc = hwi2c_write(address >> 1, buf, len, 0);
  return rc == (int)len ? 0 : (rc < 0 ? status(rc) : I2C_ERROR_DATA_NACK);
}

uint8_t i2c_read_blocks(uint8_t address, i2cblock_t *head, unsigned char writeblocks) {
  uint8_t buf[256];
  unsigned len = gather(head, writeblocks, buf, sizeof(buf));
  if (!len) return I2C_ERROR_DATA_NACK;
  int rc = hwi2c_write(address >> 1, buf, len, 1);
  if (rc != (int)len) return rc < 0 ? status(rc) : I2C_ERROR_DATA_NACK;
  i2cblock_t *b = head;
  for (unsigned i = 0; i < writeblocks && b; i++) b = b->next;
  for (; b; b = b->next) {
    rc = hwi2c_read(address >> 1, b->data, b->length, b->next != NULL);
    if (rc != (int)b->length) return rc < 0 ? status(rc) : I2C_ERROR_DATA_NACK;
  }
  return 0;
}

#endif
