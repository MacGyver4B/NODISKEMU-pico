/* analog-buttons.c - Tasten des petSD+ (Widerstandsleiter an ADC7) am
   petSD2 (RP2350). Auswertung wie src/avr/analog-buttons.c: gleiche
   Fenster, Hysterese, Median aus fuenf Werten, Startprüfung.

   Unterschied zum AVR: Die Leiter liegt an 5 V, der ADC des RP2350 misst
   mit 12 Bit gegen 3,3 V hinter einem Teiler (56k oben, 100k unten) auf dem
   Adapter. Jeder Messwert wird auf die 10-Bit-Skala des AVR (Referenz 5 V)
   umgerechnet, damit die Fenster unveraendert bleiben. Werte ueber 1023
   (5-V-Schiene etwas zu hoch) werden wie beim AVR auf 1023 begrenzt.
   Achtung: nicht ratiometrisch wie beim AVR (AVCC = 5 V). Weicht die 5-V-
   Schiene um mehr als etwa 2 % ab, kippen Ruhe und PREV ineinander. */
#include <stdbool.h>
#include <stdint.h>
#include "config.h"

#ifdef CONFIG_ONBOARD_DISPLAY
#include "timer.h"
#include "analog-buttons.h"
#include "hardware/adc.h"

#define SAMPLES 5
#define HYSTERESIS 4
#define ADC_MIN 540
#define ADC_MAX 1023

#ifndef PSD2_BUTTONS_RTOP
#define PSD2_BUTTONS_RTOP 56     /* kOhm, Teiler oben */
#endif
#ifndef PSD2_BUTTONS_RBOT
#define PSD2_BUTTONS_RBOT 100    /* kOhm, Teiler unten */
#endif
#define ADC_VREF_MV   3300
#define LADDER_MV     5000

static const struct {
  uint16_t low, high;
  uint8_t keys;
} windows[] = {
  {540, 585, 0},
  {593, 625, KEY_PREV},
  {633, 671, KEY_NEXT},
  {679, 733, KEY_PREV | KEY_NEXT},
  {741, 804, KEY_SEL},
  {812, 881, KEY_PREV | KEY_SEL},
  {889, 973, KEY_NEXT | KEY_SEL},
  {981, 1023, KEY_PREV | KEY_NEXT | KEY_SEL}
};

static uint16_t samples[SAMPLES];
static uint8_t sample_count, sample_pos, previous;
static bool ready, discard_first;
static volatile uint8_t boot_keys, boot_sequence, sample_sequence;

/* 12-Bit-Rohwert (0..4095) -> AVR-Skala (0..1023 bei 5 V) */
uint16_t analog_buttons_scale(uint16_t raw) {
  const uint64_t den = 4096ull * PSD2_BUTTONS_RBOT * LADDER_MV;
  uint64_t v = ((uint64_t)raw * ADC_VREF_MV * (PSD2_BUTTONS_RTOP + PSD2_BUTTONS_RBOT) * 1024u
                + den / 2) / den;
  return v > ADC_MAX ? ADC_MAX : (uint16_t)v;
}

uint8_t analog_buttons_decode(uint16_t adc, uint8_t held) {
  if (adc < ADC_MIN || adc > ADC_MAX) return ANALOG_BUTTON_INVALID;

  if (held != ANALOG_BUTTON_INVALID) {
    for (uint8_t i = 0; i < sizeof(windows) / sizeof(windows[0]); i++) {
      if (windows[i].keys == held &&
          adc + HYSTERESIS >= windows[i].low &&
          adc <= windows[i].high + HYSTERESIS)
        return held;
    }
  }
  for (uint8_t i = 0; i < sizeof(windows) / sizeof(windows[0]); i++) {
    if (adc >= windows[i].low && adc <= windows[i].high)
      return windows[i].keys;
  }
  return ANALOG_BUTTON_INVALID;
}

static void update_boot_keys(uint8_t keys) {
  if (keys != boot_keys) {
    boot_keys = keys;
    boot_sequence++;
    sample_sequence = 0;
  }
}

void analog_buttons_init(void) {
  uint32_t save;
  __asm volatile ("mrs %0, primask\n cpsid i" : "=r" (save) :: "memory");
  adc_init();
  adc_gpio_init(PSD2_BUTTONS_ADC);
  adc_select_input(PSD2_BUTTONS_ADC - 40);
  sample_count = sample_pos = 0;
  previous = boot_keys = ANALOG_BUTTON_INVALID;
  boot_sequence++;
  discard_first = true;
  ready = true;
  __asm volatile ("msr primask, %0" :: "r" (save) : "memory");
}

/* einmal pro Timer-Tick (SysTick, 100 Hz) */
uint8_t analog_buttons_poll(void) {
  if (!ready) return 0;
  uint16_t adc = analog_buttons_scale(adc_read());
  if (discard_first) {
    discard_first = false;
    return 0;
  }
  if (adc < ADC_MIN || adc > ADC_MAX) {
    sample_count = sample_pos = 0;
    previous = ANALOG_BUTTON_INVALID;
    update_boot_keys(ANALOG_BUTTON_INVALID);
    return 0;
  }
  samples[sample_pos++] = adc;
  sample_sequence++;
  if (sample_pos == SAMPLES) sample_pos = 0;
  if (sample_count < SAMPLES) sample_count++;
  if (sample_count < SAMPLES) return 0;

  uint16_t sorted[SAMPLES];
  for (uint8_t i = 0; i < SAMPLES; i++) sorted[i] = samples[i];
  for (uint8_t i = 1; i < SAMPLES; i++) {
    uint16_t value = sorted[i];
    uint8_t j = i;
    while (j && sorted[j - 1] > value) {
      sorted[j] = sorted[j - 1];
      j--;
    }
    sorted[j] = value;
  }
  adc = sorted[SAMPLES / 2];
  update_boot_keys(analog_buttons_decode(adc, ANALOG_BUTTON_INVALID));
  previous = analog_buttons_decode(adc, previous);
  return previous == ANALOG_BUTTON_INVALID ? 0 : previous;
}

uint8_t analog_buttons_boot_keys(void) {
  uint8_t keys = ANALOG_BUTTON_INVALID, sequence = 0, fresh = 0, stable = 0;

  for (uint8_t i = 0; i < 40; i++) {
    delay_ms(10);
    if (i < 9) continue;
    uint32_t save;
    uint8_t now, generation, sample;
    __asm volatile ("mrs %0, primask\n cpsid i" : "=r" (save) :: "memory");
    now = boot_keys;
    generation = boot_sequence;
    sample = sample_sequence;
    __asm volatile ("msr primask, %0" :: "r" (save) : "memory");
    if (now == ANALOG_BUTTON_INVALID || sample == fresh) stable = 0;
    else if (now != keys || generation != sequence) stable = 1;
    else stable++;
    keys = now;
    sequence = generation;
    fresh = sample;
  }
  return stable >= 31 ? keys : 0;
}

#endif
