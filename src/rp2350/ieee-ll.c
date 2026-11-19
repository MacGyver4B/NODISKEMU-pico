/* ieee-ll.c - Board-Init und ATN-Interrupt fuer petSD2 (RP2350) */
#include "config.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "atn.pio.h"

static PIO atn_pio = pio0;
static uint atn_sm;
static bool atn_ready;

void psd2_board_init(void) {
  /* Datenleitungen und alle Buseingaenge */
  for (uint p = PSD2_D_BASE; p < PSD2_D_BASE + 8; p++) {
    gpio_init(p);
    gpio_pull_up(p);
  }
  for (uint p = PSD2_ATN_I; p <= PSD2_NDAC_I; p++) {
    gpio_init(p);
    gpio_pull_up(p);
  }
  /* Ausgaenge zu den 621: high = Leitung frei */
  for (uint p = PSD2_EOI_O; p <= PSD2_SRQ_O; p++) {
    gpio_init(p);
    gpio_put(p, 1);
    gpio_set_dir(p, GPIO_OUT);
  }
  gpio_init(PSD2_DIR);
  gpio_put(PSD2_DIR, 0);                 /* Daten: Empfang */
  gpio_set_dir(PSD2_DIR, GPIO_OUT);
  gpio_init(PSD2_LED);
}

static void __not_in_flash_func(atn_isr)(void) {
  pio_interrupt_clear(atn_pio, 0);
  /* wie atn-ack-petsd+.S: alles auf Hoeren, NRFD ziehen, NDAC frei */
  ieee488_DataListen();
  ieee488_SetDAV(1);
  ieee488_SetEOI(1);
  ieee488_TE75161 = 0;
  ieee488_SetNRFD(0);
  ieee488_SetNDAC(1);
  ieee488_ATN_received = true;
}

void ieee488_EnableAtnInterrupt(void)  { if (atn_ready) irq_set_enabled(PIO0_IRQ_0, true); }
void ieee488_DisableAtnInterrupt(void) { irq_set_enabled(PIO0_IRQ_0, false); }

void ieee488_InitAtnInterrupt(void) {
  if (!atn_ready) {
    pio_set_gpio_base(atn_pio, 16);
    uint off = pio_add_program(atn_pio, &atn_program);
    atn_sm = pio_claim_unused_sm(atn_pio, true);
    pio_sm_config c = atn_program_get_default_config(off);
    sm_config_set_in_pins(&c, PSD2_ATN_I);
    pio_sm_init(atn_pio, atn_sm, off, &c);
    pio_set_irq0_source_enabled(atn_pio, pis_interrupt0, true);
    irq_set_exclusive_handler(PIO0_IRQ_0, atn_isr);
    irq_set_priority(PIO0_IRQ_0, 0);     /* hoechste Prioritaet */
    pio_sm_set_enabled(atn_pio, atn_sm, true);
    atn_ready = true;
  }
  pio_interrupt_clear(atn_pio, 0);
  ieee488_EnableAtnInterrupt();
}
