/* emu.h - Postfach zwischen Firmware und psd2sim (nur PSD2_EMU)

   picoem bildet SPI und UART nicht nach. Im Emulator laufen SD-Karte
   und Debugausgabe deshalb ueber dieses Postfach am Anfang von
   SCRATCH_X (Kern 1 wird nicht benutzt). Die Firmware traegt ein
   Kommando ein und wartet, bis die Bruecke cmd auf 0 setzt. */
#ifndef PSD2_EMU_H
#define PSD2_EMU_H

#include <stdint.h>

#define EMU_MBOX_ADDR 0x20080000u

enum {
  EMU_IDLE = 0,
  EMU_SD_SELECT,      /* arg: 1 = Karte abgewaehlt (CS high), 0 = gewaehlt */
  EMU_SD_XFER,        /* arg: Byte -> res: Antwort */
  EMU_SD_TXBLOCK,     /* buf[0..len) senden */
  EMU_SD_RXBLOCK,     /* len Bytes (0xff gesendet) -> buf */
  EMU_PRINT,          /* buf[0..len) auf stderr der Bruecke */
};

struct emu_mbox {
  volatile uint32_t cmd;
  volatile uint32_t arg;
  volatile uint32_t len;
  volatile uint32_t res;
  volatile uint8_t  buf[1024];
};

#define EMU_MBOX ((struct emu_mbox *)EMU_MBOX_ADDR)

static inline void emu_call(uint32_t cmd) {
  __asm volatile ("dmb" ::: "memory");
  EMU_MBOX->cmd = cmd;
  while (EMU_MBOX->cmd != EMU_IDLE)
    ;
  __asm volatile ("dmb" ::: "memory");
}

#endif
