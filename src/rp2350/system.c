/* system.c - Systemfunktionen fuer petSD2 (RP2350) */
#include "config.h"
#include "hardware/watchdog.h"
#include "system.h"

void system_init_early(void) {}
void system_init_late(void) {}

void system_sleep(void) {
  __asm volatile ("wfi");
}

void system_reset(void) {
  watchdog_reboot(0, 0, 0);
  for (;;)
    ;
}

/* eigene Fassung, hardware/sync.h hat gleichnamige Inline-Funktionen */
void disable_interrupts(void) { __asm volatile ("cpsid i" ::: "memory"); }
void enable_interrupts(void)  { __asm volatile ("cpsie i" ::: "memory"); }
