/* ieee-ll.c - Board-Init und ATN-Interrupt fuer petSD2 (RP2350) */
#include "config.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "atn.pio.h"
#include "pico/platform.h"
#include <stdio.h>

static PIO atn_pio = pio0;
static uint atn_sm;
static bool atn_ready;

void psd2_board_init(void) {
  /* Daten und Eingaenge vom 75161 */
  for (uint p = PSD2_D_BASE; p < PSD2_D_BASE + 8; p++) {
    gpio_init(p);
    gpio_pull_up(p);
  }
  for (uint p = PSD2_ATN; p <= PSD2_NDAC; p++) {
    gpio_init(p);
    gpio_pull_up(p);
  }
  /* Grundzustand Hoeren: NRFD, NDAC frei und als Ausgang, TE low */
  gpio_put(PSD2_NRFD, 1);
  gpio_put(PSD2_NDAC, 1);
  gpio_init(PSD2_TE);
  gpio_put(PSD2_TE, 0);
  gpio_set_dir(PSD2_TE, GPIO_OUT);
  gpio_set_dir(PSD2_NRFD, GPIO_OUT);
  gpio_set_dir(PSD2_NDAC, GPIO_OUT);
  gpio_init(PSD2_SRQ);
  gpio_put(PSD2_SRQ, 1);
  gpio_set_dir(PSD2_SRQ, GPIO_OUT);
  gpio_init(PSD2_LED);
}

static void __not_in_flash_func(atn_isr)(void) {
  pio_interrupt_clear(atn_pio, 0);
  /* wie atn-ack-petsd+.S: alles auf Hoeren, NRFD ziehen, NDAC frei */
  ieee488_SetNRFD(0);
  ieee488_SetNDAC(1);
  ieee488_DataListen();
  ieee488_CtrlPortsListen();
  ieee488_ATN_received = true;
}

void ieee488_EnableAtnInterrupt(void)  { if (atn_ready) irq_set_enabled(PIO0_IRQ_0, true); }
void ieee488_DisableAtnInterrupt(void) { irq_set_enabled(PIO0_IRQ_0, false); }

void ieee488_InitAtnInterrupt(void) {
  if (!atn_ready) {
    /* Revision 2 = A2: GPIOs nicht offiziell 5-V-tolerant; die 7516x
       liefern TTL-Pegel aus 5 V, dafuer braucht es A4 (Revision > 2) */
    unsigned rev = rp2350_chip_version();
    printf("RP2350 Revision %u%s\r\n", rev, rev <= 2 ? " - A2 oder aelter, 7516x-Pegel pruefen!" : "");
    pio_set_gpio_base(atn_pio, 16);
    uint off = pio_add_program(atn_pio, &atn_program);
    atn_sm = pio_claim_unused_sm(atn_pio, true);
    pio_sm_config c = atn_program_get_default_config(off);
    sm_config_set_in_pins(&c, PSD2_ATN);
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
