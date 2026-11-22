/* arch-timer.c - Zeitbasis fuer petSD2 (RP2350)

   ticks (100 Hz) per SysTick, Verzoegerungen und Timeouts ueber den
   1-MHz-Zeitgeber TIMER0. */
#include "config.h"
#include "hardware/timer.h"
#include "hardware/structs/systick.h"
#include "hardware/clocks.h"
#include "led.h"
#include "timer.h"
#include "iec-host.h"
#ifdef HAVE_ANALOG_BUTTONS
#include "analog-buttons.h"
#endif

static uint64_t timeout_end;

uint64_t __not_in_flash_func(iec_now)(void) {
  static uint32_t last;
  static uint64_t high;
  uint32_t save, c;
  __asm volatile ("mrs %0, primask\n cpsid i" : "=r" (save) :: "memory");
  c = *(volatile uint32_t *)0xE0001004u;            /* DWT_CYCCNT */
  if (c < last)
    high += 1ull << 32;
  last = c;
  uint64_t r = high | c;
  __asm volatile ("msr primask, %0" :: "r" (save) : "memory");
  return r;
}

#ifdef HAVE_ANALOG_BUTTONS
/* Entprellen wie timerint.S (vier Abtastungen per vertikalem Zaehler),
   Wiederholung fuer PREV und NEXT nach 500 ms, dann alle 150 ms */
#define REPEAT_MASK     (KEY_PREV | KEY_NEXT)
#define REPEAT_START    50
#define REPEAT_NEXT     15

extern volatile uint8_t key_state, key_press, key_rpt;
extern volatile uint8_t key_ct0, key_ct1, key_ct2, key_repeat_counter;

static void debounce_keys(uint8_t sample) {
  uint8_t delta = key_state ^ sample;
  key_ct2 = (key_ct2 ^ (key_ct1 & key_ct0)) & delta;
  key_ct1 = (key_ct1 ^ key_ct0) & delta;
  key_ct0 = ~key_ct0 & delta;
  uint8_t toggle = delta ^ (key_ct0 | key_ct1 | key_ct2);
  uint8_t state = key_state ^ toggle;
  key_state = state;
  key_press |= state & toggle;
  if (!(state & REPEAT_MASK))
    key_repeat_counter = REPEAT_START;
  if (--key_repeat_counter == 0) {
    key_repeat_counter = REPEAT_NEXT;
    key_rpt |= state & REPEAT_MASK;
  }
}
#endif

void isr_systick(void) {
  (void)iec_now();             /* Ueberlauf des Zykluszaehlers mitzaehlen */
  ticks++;
  /* Fehler-LED blinken lassen (wie timerint.S) */
  if ((led_state & LED_ERROR) && (ticks & 15) == 0)
    toggle_dirty_led();
#ifdef HAVE_ANALOG_BUTTONS
  debounce_keys(analog_buttons_poll());
#endif
}

/* Zeitbasis ist der DWT-Zykluszaehler (sys_clk): der SDK-Zeitgeber
   haengt am Referenztakt, der in picoem nicht 12 MHz hat */
void timer_init(void) {
  uint32_t hz = clock_get_hz(clk_sys);
  *(volatile uint32_t *)0xE000EDFCu |= 1u << 24;     /* DEMCR.TRCENA */
  *(volatile uint32_t *)0xE0001000u |= 1u;           /* DWT_CTRL.CYCCNTENA */
  iec_cyc_per_us = hz / 1000000u;
  systick_hw->csr = 0;
  systick_hw->rvr = hz / HZ - 1;
  systick_hw->cvr = 0;
  systick_hw->csr = 0x7;       /* CLKSOURCE = Prozessortakt, TICKINT, ENABLE */
}

void delay_us(unsigned int time) {
  uint64_t end = iec_now() + (uint64_t)time * iec_cyc_per_us;
  while (iec_now() < end)
    ;
}

void delay_ms(unsigned int time) {
  while (time--)
    delay_us(1000);
}

void start_timeout(unsigned int usecs) {
  timeout_end = iec_now() + (uint64_t)usecs * iec_cyc_per_us;
}

unsigned int has_timed_out(void) {
  return iec_now() >= timeout_end;
}
