/* iec-host.h - IEC-Bus als Rechner fuer petSD2 (RP2350): Pins und Zeit

   Belegung laut Konzept (18.11.):
     39 CLK_I  40 DATA_I  41 ATN_I  42 SRQ_I      ueber 74LVC14A (invertiert:
                                                   Pin high = Leitung low)
     43 CLK_O  44 DATA_O  45 ATN_O  46 SRQ_O  47 RESET_O   ueber 74LS07
                                                   (Pin low = Leitung low)

   Leitungsbits wie in relay.c: gesetzt = Leitung low.
   Zeit: DWT-Zykluszaehler, auf 64 Bit erweitert (sys_clk, 150 MHz). */
#ifndef IEC_HOST_H
#define IEC_HOST_H

#include <stdint.h>
#include "hardware/structs/sio.h"

#define IEC_ATN   0x01
#define IEC_CLK   0x02
#define IEC_DATA  0x04
#define IEC_SRQ   0x08
#define IEC_RESET 0x10

#define IEC_CLK_I   39
#define IEC_DATA_I  40
#define IEC_ATN_I   41
#define IEC_SRQ_I   42
#define IEC_CLK_O   43
#define IEC_DATA_O  44
#define IEC_ATN_O   45
#define IEC_SRQ_O   46
#define IEC_RESET_O 47

#define IEC_HI(p) (1u << ((p) - 32))

extern uint32_t iec_cyc_per_us;

/* 64-Bit-Zeit in Takten (DWT_CYCCNT erweitert, arch-timer.c); oefter als
   alle 28 s aufrufen, der SysTick erledigt das ohnehin */
uint64_t iec_now(void);

static inline uint8_t iec_in(void) {
  uint32_t h = sio_hw->gpio_hi_in;
  uint8_t v = 0;
  if (h & IEC_HI(IEC_ATN_I))  v |= IEC_ATN;
  if (h & IEC_HI(IEC_CLK_I))  v |= IEC_CLK;
  if (h & IEC_HI(IEC_DATA_I)) v |= IEC_DATA;
  if (h & IEC_HI(IEC_SRQ_I))  v |= IEC_SRQ;
  return v;
}

static inline void iec_out(uint8_t pull) {
  uint32_t lo = 0;                       /* Pins, die auf low muessen */
  if (pull & IEC_ATN)   lo |= IEC_HI(IEC_ATN_O);
  if (pull & IEC_CLK)   lo |= IEC_HI(IEC_CLK_O);
  if (pull & IEC_DATA)  lo |= IEC_HI(IEC_DATA_O);
  if (pull & IEC_SRQ)   lo |= IEC_HI(IEC_SRQ_O);
  if (pull & IEC_RESET) lo |= IEC_HI(IEC_RESET_O);
  const uint32_t all = IEC_HI(IEC_ATN_O) | IEC_HI(IEC_CLK_O) | IEC_HI(IEC_DATA_O) |
                       IEC_HI(IEC_SRQ_O) | IEC_HI(IEC_RESET_O);
  sio_hw->gpio_hi_set = all & ~lo;
  sio_hw->gpio_hi_clr = lo;
}

void iec_host_init(void);

/* AR6-Senden per PIO (iec-host.c) */
#include <stdbool.h>
extern bool iec_pio_mode;
void iec_pio_begin(uint32_t levels);
void iec_pio_end(void);
void iec_pio_put(uint32_t word);

#endif
