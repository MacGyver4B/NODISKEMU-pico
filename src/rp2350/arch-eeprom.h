/* arch-eeprom.h - EEPROM fuer petSD2 (RP2350)

   NODISKEMU spricht das EEPROM wie beim AVR ueber EEMEM-Variablen und
   eeprom_read/write_* an, teils auch ueber rohe Adressen ab 0 (Pruefsumme
   in eeprom-conf.c). Hier steht dahinter ein 2-KB-Abbild im RAM mit den
   Adressen 0..2047:
     - EEMEM-Variablen liegen im Abschnitt psd2_ee; ihre Adresse zaehlt nur
       als Abstand zum Anfang des Abschnitts (gelesen wird nie direkt),
     - kleine Zahlen (< 2048) sind schon EEPROM-Adressen.
   Woher das Abbild kommt und wohin es geschrieben wird (I2C-EEPROM oder
   Datei PETSD2.EEP), regelt arch-eeprom.c; eeprom_safety() schreibt. */
#ifndef ARCH_EEPROM_H
#define ARCH_EEPROM_H

#include <stdint.h>
#include <string.h>

#define PSD2_EE_SIZE 2048

#define EEMEM __attribute__((section("psd2_ee"), used))

extern uint8_t psd2_ee_img[PSD2_EE_SIZE];
extern char __start_psd2_ee[];
void psd2_ee_sync_in(void);              /* bei Bedarf laden */
void psd2_ee_dirty(unsigned off, unsigned n);
void psd2_ee_flush(void);

static inline unsigned psd2_ee_off(const void *a) {
  uintptr_t p = (uintptr_t)a;
  if (p >= PSD2_EE_SIZE)
    p -= (uintptr_t)__start_psd2_ee;
  return (unsigned)p & (PSD2_EE_SIZE - 1);
}

static inline uint8_t eeprom_read_byte(const void *a) {
  psd2_ee_sync_in();
  return psd2_ee_img[psd2_ee_off(a)];
}
static inline uint16_t eeprom_read_word(const void *a) {
  unsigned o = psd2_ee_off(a);
  psd2_ee_sync_in();
  return psd2_ee_img[o] | (psd2_ee_img[(o + 1) & (PSD2_EE_SIZE - 1)] << 8);
}
static inline void eeprom_read_block(void *d, const void *a, unsigned int n) {
  unsigned o = psd2_ee_off(a);
  psd2_ee_sync_in();
  for (unsigned i = 0; i < n; i++)
    ((uint8_t *)d)[i] = psd2_ee_img[(o + i) & (PSD2_EE_SIZE - 1)];
}
static inline void eeprom_write_byte(void *a, uint8_t v) {
  unsigned o = psd2_ee_off(a);
  if (psd2_ee_img[o] != v) {
    psd2_ee_img[o] = v;
    psd2_ee_dirty(o, 1);
  }
}
static inline void eeprom_write_block(const void *s, void *a, unsigned int n) {
  unsigned o = psd2_ee_off(a);
  for (unsigned i = 0; i < n; i++)
    eeprom_write_byte((void *)(uintptr_t)((o + i) & (PSD2_EE_SIZE - 1)), ((const uint8_t *)s)[i]);
}
static inline void eeprom_write_word(void *a, uint16_t v) {
  uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
  eeprom_write_block(b, a, 2);
}
#define eeprom_update_byte  eeprom_write_byte
#define eeprom_update_word  eeprom_write_word
#define eeprom_update_block eeprom_write_block

/* nach einer Folge von Schreibzugriffen: Abbild sichern */
#define eeprom_safety() psd2_ee_flush()

#endif
