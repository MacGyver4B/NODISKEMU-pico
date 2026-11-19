/* crc.c - CRC-Routinen fuer petSD2 (RP2350), C statt LPC-Assembler */
#include <stdint.h>
#include "crc.h"

uint8_t crc7update(uint8_t crc, uint8_t data) {
  for (int i = 0; i < 8; i++) {
    crc <<= 1;
    if ((data & 0x80) ^ (crc & 0x80))
      crc ^= 0x09;
    data <<= 1;
  }
  return crc & 0x7f;
}

uint16_t crc_xmodem_update(uint16_t crc, uint8_t data) {
  crc ^= (uint16_t)data << 8;
  for (int i = 0; i < 8; i++)
    crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  return crc;
}

uint16_t crc_xmodem_block(uint16_t crc, const uint8_t *data, uint32_t length) {
  while (length--)
    crc = crc_xmodem_update(crc, *data++);
  return crc;
}

uint16_t crc16_update(uint16_t crc, uint8_t a) {
  crc ^= a;
  for (int i = 0; i < 8; i++)
    crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
  return crc;
}
