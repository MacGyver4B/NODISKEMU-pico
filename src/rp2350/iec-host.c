/* iec-host.c - IEC-Pins und Zeitbasis fuer das Relais (petSD2, RP2350) */
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "config.h"
#include "iec-host.h"

#define IEC_IN_WIN(p) ((p) >= PSD2_PIO_GPIO_BASE && (p) <= PSD2_PIO_GPIO_BASE + 31)
#if !IEC_IN_WIN(IEC_CLK_O) || !IEC_IN_WIN(IEC_DATA_O) || !IEC_IN_WIN(IEC_CLK_I) || \
    !IEC_IN_WIN(IEC_DATA_I) || !IEC_IN_WIN(IEC_ATN_I) || !IEC_IN_WIN(IEC_SRQ_I)
#  error "IEC-Pins muessen im PIO-Fenster liegen (PSD2_PIO_GPIO_BASE..+31)"
#endif
#include "ar6put.pio.h"

/* PIO1 fuer die AR6-Senderoutine (CLK_O/DATA_O); PIO0 hat ATN */
static PIO apio = pio1;
static uint asm_sm;
bool iec_pio_mode;
/* 64-Bit-Maske zur Laufzeit bauen: als Konstante laedt GCC sie per
   vldr d7, das picoem (nur Single-Precision-FPU) nicht kennt */
static volatile uint32_t pio_mask_lo = IEC_LO(IEC_CLK_O) | IEC_LO(IEC_DATA_O);
static volatile uint32_t pio_mask_hi = IEC_HI(IEC_CLK_O) | IEC_HI(IEC_DATA_O);
static uint64_t pio_mask(void) { return ((uint64_t)pio_mask_hi << 32) | pio_mask_lo; }

uint32_t iec_cyc_per_us;

void iec_host_init(void) {
  static const uint8_t ins[]  = { IEC_CLK_I, IEC_DATA_I, IEC_ATN_I, IEC_SRQ_I };
  static const uint8_t outs[] = { IEC_CLK_O, IEC_DATA_O, IEC_ATN_O, IEC_SRQ_O, IEC_RESET_O };
  for (uint i = 0; i < sizeof ins; i++) {
    gpio_init(ins[i]);                    /* Eingaenge vom Eingangspuffer */
  }
  for (uint i = 0; i < sizeof outs; i++) {
    gpio_init(outs[i]);
    gpio_put(outs[i], 1);                 /* high = Leitung frei (74xx07) */
    gpio_set_dir(outs[i], GPIO_OUT);
  }
  /* DWT-Zykluszaehler einschalten: DEMCR.TRCENA, DWT_CTRL.CYCCNTENA */
  *(volatile uint32_t *)0xE000EDFCu |= 1u << 24;
  *(volatile uint32_t *)0xE0001000u |= 1u;
  iec_cyc_per_us = clock_get_hz(clk_sys) / 1000000u;

  pio_set_gpio_base(apio, PSD2_PIO_GPIO_BASE);
  uint off = pio_add_program(apio, &ar6put_program);
  asm_sm = pio_claim_unused_sm(apio, true);
  pio_sm_config c = ar6put_program_get_default_config(off);
  sm_config_set_out_pins(&c, IEC_CLK_O, 2);
  sm_config_set_out_shift(&c, true, false, 32);     /* LSB zuerst, kein Autopull */
  sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / 1000000.0f);
  pio_sm_init(apio, asm_sm, off, &c);
  pio_sm_set_enabled(apio, asm_sm, true);
}

/* Leitungen an die PIO uebergeben; levels: Bit 0 CLK, Bit 1 DATA, 1 = frei */
void iec_pio_begin(uint32_t levels) {
  uint64_t mask = pio_mask();
  pio_sm_set_pins_with_mask64(apio, asm_sm, (uint64_t)(levels & 3u) << IEC_CLK_O, mask);
  pio_sm_set_pindirs_with_mask64(apio, asm_sm, mask, mask);
  gpio_set_function(IEC_CLK_O, GPIO_FUNC_PIO1);
  gpio_set_function(IEC_DATA_O, GPIO_FUNC_PIO1);
  iec_pio_mode = true;
}

/* zurueck an den SIO; die SIO-Ausgaenge muessen vorher stimmen */
void iec_pio_end(void) {
  uint64_t mask = pio_mask();
  gpio_set_function(IEC_CLK_O, GPIO_FUNC_SIO);
  gpio_set_function(IEC_DATA_O, GPIO_FUNC_SIO);
  pio_sm_set_pindirs_with_mask64(apio, asm_sm, 0, mask);
  iec_pio_mode = false;
}

/* ein Byte senden (Wort mit 6 Zustaenden) und auf das Ende warten */
void __not_in_flash_func(iec_pio_put)(uint32_t word) {
  pio_sm_put(apio, asm_sm, word);
  while (pio_sm_is_rx_fifo_empty(apio, asm_sm))
    ;
  (void)pio_sm_get(apio, asm_sm);
}
