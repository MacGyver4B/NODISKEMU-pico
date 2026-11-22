/* diagnose.c - Hardwaretest des petSD2 (RP2350), Gegenstueck zu
   src/diagnose.c des petSD+: PREV beim Einschalten halten.

   Zeile 1: Tasten-ADC (AVR-Skala wie beim petSD+), Kartenschalter, Schreibschutz
   Zeile 2: Chip-Revision statt der AVR-Fuses
   Zeile 3: Version des ATtiny25 (PWM), "--" wenn er nicht antwortet
   Die LEDs blinken abwechselnd. Endlosschleife wie beim petSD+. */
#include <stdio.h>
#include "config.h"

#ifdef CONFIG_ONBOARD_DISPLAY
#include "timer.h"
#include "led.h"
#include "i2c.h"
#include "lcd.h"
#include "diagnose.h"
#include "analog-buttons.h"
#include "hardware/adc.h"
#include "pico/platform.h"

void board_diagnose(void) {
  char buffer[32];
  unsigned int counter = 0;

  lcd_init();
  lcd_bootscreen();
  lcd_clear();
  sdcard_interface_init();

  //               0....:....1....:....
  lcd_puts_P(PSTR("ADC: ---- CD:- WP:\n"));
  snprintf(buffer, sizeof(buffer), "RP2350 Rev. %u", rp2350_chip_version());
  lcd_puts(buffer);

  lcd_locate(0, 2); lcd_puts_P(PSTR("PWM:"));
  int16_t res = i2c_read_register(I2C_SLAVE_ADDRESS, I2C_SOFTWARE_VERSION_COMPAT);
  if (res < 0) lcd_puts_P(PSTR("--"));
  else lcd_printf("%02X", res);

  for (;;) {
    set_busy_led(counter & 4);
    set_dirty_led(!(counter & 4));

    lcd_locate(5, 0);
    snprintf(buffer, sizeof(buffer), "%4u", analog_buttons_scale(adc_read()));
    lcd_puts(buffer);

    lcd_locate(13, 0);
    lcd_putc(sdcard_detect() ? 'Y' : 'N');

    lcd_locate(18, 0);
    lcd_putc(sdcard_wp() ? 'Y' : 'N');

    delay_ms(200);
    counter++;
  }
}
#else
void board_diagnose(void) {}
#endif
