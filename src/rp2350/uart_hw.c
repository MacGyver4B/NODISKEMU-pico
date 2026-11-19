/* uart_hw.c - UART0 (GPIO 0/1) fuer die Debugausgabe, nur echte Hardware */
#ifndef PSD2_EMU
#include "hardware/uart.h"
#include "hardware/gpio.h"

void hwuart_init(unsigned baud) {
  uart_init(uart0, baud);
  gpio_set_function(0, GPIO_FUNC_UART);
  gpio_set_function(1, GPIO_FUNC_UART);
}
void hwuart_putc(char c) { uart_putc_raw(uart0, c); }
int  hwuart_getc(void)   { return uart_is_readable(uart0) ? uart_getc(uart0) : -1; }
void hwuart_flush(void)  { uart_tx_wait_blocking(uart0); }
#endif
