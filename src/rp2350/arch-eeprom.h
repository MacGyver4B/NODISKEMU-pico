/* arch-eeprom.h - EEPROM-Ersatz fuer petSD2 (RP2350): vorerst im RAM */
#ifndef ARCH_EEPROM_H
#define ARCH_EEPROM_H

#include <stdint.h>
#include <string.h>

/* EEMEM-Variablen liegen als normale Variablen im RAM; ohne Flash-
   Ablage beginnt jeder Start mit leerer Konfiguration (Pruefsumme
   falsch -> Vorgaben). */
#define EEMEM __attribute__((section(".bss.psd2_eeprom")))
#define eeprom_safety() do {} while (0)

static inline uint8_t  eeprom_read_byte(void *a)  { return *(uint8_t *)a; }
static inline uint16_t eeprom_read_word(void *a)  { uint16_t v; memcpy(&v, a, 2); return v; }
static inline void eeprom_read_block(void *d, void *a, unsigned int n) { memcpy(d, a, n); }
static inline void eeprom_write_byte(void *a, uint8_t v)  { *(uint8_t *)a = v; }
static inline void eeprom_write_word(void *a, uint16_t v) { memcpy(a, &v, 2); }
static inline void eeprom_write_block(void *s, void *a, unsigned int n) { memcpy(a, s, n); }

#endif
