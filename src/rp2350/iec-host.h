/* iec-host.h - IEC-Bus als Rechner fuer petSD2 (RP2350): Pins und Zeit

   Vorgabe-Belegung (ueberschreibbar in pico/boards/<board>.h, beliebige
   Pins, nur DATA_O = CLK_O + 1):
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

#ifndef IEC_CLK_I
#define IEC_CLK_I   39
#endif
#ifndef IEC_DATA_I
#define IEC_DATA_I  40
#endif
#ifndef IEC_ATN_I
#define IEC_ATN_I   41
#endif
#ifndef IEC_SRQ_I
#define IEC_SRQ_I   42
#endif
#ifndef IEC_CLK_O
#define IEC_CLK_O   43
#endif
#ifndef IEC_DATA_O
#define IEC_DATA_O  44
#endif
#ifndef IEC_ATN_O
#define IEC_ATN_O   45
#endif
#ifndef IEC_SRQ_O
#define IEC_SRQ_O   46
#endif
#ifndef IEC_RESET_O
#define IEC_RESET_O 47
#endif

/* Pruefungen: jeder Pin darf im unteren oder oberen SIO-Register liegen
   (die 74LVC07A/74LVC14A arbeiten mit 3,3 V, auch die ADC-Pins 40-47 sind
   erlaubt); DATA_O muss direkt hinter CLK_O liegen, die PIO fuer AR6 setzt
   beide mit einem Befehl. */
#if IEC_CLK_I < 0 || IEC_CLK_I > 47 || IEC_DATA_I < 0 || IEC_DATA_I > 47 || \
    IEC_ATN_I < 0 || IEC_ATN_I > 47 || IEC_SRQ_I < 0 || IEC_SRQ_I > 47 || \
    IEC_CLK_O < 0 || IEC_RESET_O > 47 || IEC_ATN_O > 47 || IEC_SRQ_O > 47 || IEC_DATA_O > 47
#  error "IEC-Pins muessen in GPIO 0-47 liegen"
#endif
#if IEC_DATA_O != IEC_CLK_O + 1 || IEC_DATA_O == 32
#  error "IEC_DATA_O muss direkt hinter IEC_CLK_O liegen (im selben SIO-Register)"
#endif

/* Bit im unteren bzw. oberen SIO-Register, 0 wenn der Pin im anderen liegt */
#define IEC_LO(p) ((p) < 32 ? 1u << ((p) & 31) : 0u)
#define IEC_HI(p) ((p) >= 32 ? 1u << ((p) & 31) : 0u)
#define IEC_OUT_LO (IEC_LO(IEC_ATN_O) | IEC_LO(IEC_CLK_O) | IEC_LO(IEC_DATA_O) | \
                    IEC_LO(IEC_SRQ_O) | IEC_LO(IEC_RESET_O))
#define IEC_OUT_HI (IEC_HI(IEC_ATN_O) | IEC_HI(IEC_CLK_O) | IEC_HI(IEC_DATA_O) | \
                    IEC_HI(IEC_SRQ_O) | IEC_HI(IEC_RESET_O))

extern uint32_t iec_cyc_per_us;

/* 64-Bit-Zeit in Takten (DWT_CYCCNT erweitert, arch-timer.c); oefter als
   alle 28 s aufrufen, der SysTick erledigt das ohnehin */
uint64_t iec_now(void);

static inline uint8_t iec_in(void) {
  uint32_t l = sio_hw->gpio_in;
  uint32_t h = sio_hw->gpio_hi_in;
  uint8_t v = 0;
  if ((l & IEC_LO(IEC_ATN_I))  | (h & IEC_HI(IEC_ATN_I)))  v |= IEC_ATN;
  if ((l & IEC_LO(IEC_CLK_I))  | (h & IEC_HI(IEC_CLK_I)))  v |= IEC_CLK;
  if ((l & IEC_LO(IEC_DATA_I)) | (h & IEC_HI(IEC_DATA_I))) v |= IEC_DATA;
  if ((l & IEC_LO(IEC_SRQ_I))  | (h & IEC_HI(IEC_SRQ_I)))  v |= IEC_SRQ;
  return v;
}

static inline void iec_out(uint8_t pull) {
  uint32_t lo = 0, hi = 0;               /* Pins, die auf low muessen */
  if (pull & IEC_ATN)   { lo |= IEC_LO(IEC_ATN_O);   hi |= IEC_HI(IEC_ATN_O); }
  if (pull & IEC_CLK)   { lo |= IEC_LO(IEC_CLK_O);   hi |= IEC_HI(IEC_CLK_O); }
  if (pull & IEC_DATA)  { lo |= IEC_LO(IEC_DATA_O);  hi |= IEC_HI(IEC_DATA_O); }
  if (pull & IEC_SRQ)   { lo |= IEC_LO(IEC_SRQ_O);   hi |= IEC_HI(IEC_SRQ_O); }
  if (pull & IEC_RESET) { lo |= IEC_LO(IEC_RESET_O); hi |= IEC_HI(IEC_RESET_O); }
  if (IEC_OUT_HI) {
    sio_hw->gpio_hi_set = IEC_OUT_HI & ~hi;
    sio_hw->gpio_hi_clr = hi;
  }
  if (IEC_OUT_LO) {
    sio_hw->gpio_set = IEC_OUT_LO & ~lo;
    sio_hw->gpio_clr = lo;
  }
}

void iec_host_init(void);

/* AR6-Senden per PIO (iec-host.c) */
#include <stdbool.h>
extern bool iec_pio_mode;
void iec_pio_begin(uint32_t levels);
void iec_pio_end(void);
void iec_pio_put(uint32_t word);

#endif
