/* uart.c - Debugausgabe fuer petSD2 (RP2350)

   Echte Hardware: UART0 an GPIO 0/1. Im Emulator: Zeilen ueber das
   Postfach an psd2sim. printf() geht ueber einen eigenen stdio-Treiber
   in denselben Kanal. */
#include "config.h"
#include <stdio.h>
#include "pico/stdio.h"
#include "pico/stdio/driver.h"
#ifdef PSD2_EMU
#  include "emu.h"
#else
/* SDK-UART in uart_hw.c, dessen Namen kollidieren mit uart.h */
void hwuart_init(unsigned baud);
void hwuart_putc(char c);
int  hwuart_getc(void);
void hwuart_flush(void);
#endif
#include "uart.h"

#ifdef PSD2_EMU
static char line[200];
static unsigned int linelen;
#endif

void uart_flush(void) {
#ifdef PSD2_EMU
  if (linelen) {
    for (unsigned int i = 0; i < linelen; i++)
      EMU_MBOX->buf[i] = line[i];
    EMU_MBOX->len = linelen;
    emu_call(EMU_PRINT);
    linelen = 0;
  }
#else
  hwuart_flush();
#endif
}

void uart_putc(char c) {
#ifdef PSD2_EMU
  line[linelen++] = c;
  if (c == '\n' || linelen == sizeof line)
    uart_flush();
#else
  hwuart_putc(c);
#endif
}

unsigned char uart_getc(void) {
#ifdef PSD2_EMU
  return 0;
#else
  int c = hwuart_getc();
  return c < 0 ? 0 : (unsigned char)c;
#endif
}

void uart_puthex(uint8_t num) {
  static const char hex[] = "0123456789ABCDEF";
  uart_putc(hex[num >> 4]);
  uart_putc(hex[num & 15]);
}

void uart_puts_P(const char *text) {
  while (*text)
    uart_putc(*text++);
}

void uart_putcrlf(void) {
  uart_putc('\r');
  uart_putc('\n');
}

static void drv_out(const char *buf, int len) {
  while (len--)
    uart_putc(*buf++);
}

static stdio_driver_t drv = {
  .out_chars = drv_out,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
  .crlf_enabled = false,
#endif
};

void uart_init(void) {
#ifndef PSD2_EMU
  hwuart_init(CONFIG_UART_BAUDRATE);
#endif
  stdio_set_driver_enabled(&drv, true);
  setvbuf(stdout, NULL, _IONBF, 0);     /* printf sofort, nicht gepuffert */
}

void uart_trace(void *ptr, uint16_t start, uint16_t len) {
  uint16_t i;
  uint8_t j;
  uint8_t ch;
  uint8_t *data = ptr;

  data+=start;
  for(i=0;i<len;i+=16) {

    uart_puthex(start>>8);
    uart_puthex(start&0xff);
    uart_putc('|');
    uart_putc(' ');
    for(j=0;j<16;j++) {
      if(i+j<len) {
        ch=*(data + j);
        uart_puthex(ch);
      } else {
        uart_putc(' ');
        uart_putc(' ');
      }
      uart_putc(' ');
    }
    uart_putc('|');
    for(j=0;j<16;j++) {
      if(i+j<len) {
        ch=*(data++);
        if(ch<32 || ch>0x7e)
          ch='.';
        uart_putc(ch);
      } else {
        uart_putc(' ');
      }
    }
    uart_putc('|');
    uart_putcrlf();
    start+=16;
  }
}
