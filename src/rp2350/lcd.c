/* lcd.c - LCD 20x4 (HD44780/WS0010) am petSD2 (RP2350), 4 Bit, nur schreiben

   Ablauf, Wartezeiten und Zeichensatz wie src/avr/lcd.c (petSD+), nur die
   Portzugriffe sind ersetzt: die Datenleitungen DB4-DB7 liegen auf
   beliebigen GPIOs (Board-Datei PSD2_LCD_*). R/W liegt auf dem petSD+ fest
   auf GND, deshalb wird nie gelesen und mit festen Zeiten gewartet.
   Helligkeit und Kontrast stellt der ATtiny25 per PWM (I2C). */
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>

#include "config.h"

#ifdef CONFIG_ONBOARD_DISPLAY
#include "lcd.h"
#include "timer.h"
#include "i2c.h"
#include "hardware/gpio.h"

#define LCD_DELAY_US_DATA       92
#define LCD_DELAY_US_E          40

#define LCD_DELAY_MS_POWERON   500
#define LCD_DELAY_MS_CLEAR      20
#define LCD_DELAY_MS_LONG       10
#define LCD_DELAY_MS_SHORT       2

#define LCD_DDRAM 128

uint8_t lcd_x;                  // 0..LCD_COLS-1
uint8_t lcd_y;                  // 0..LCD_LINES-1
uint8_t lcd_contrast = 2;       // Vorgaben wie petSD+
uint8_t lcd_brightness = 255;

static bool lcd_ready;
static const uint8_t lcd_db[4] = { PSD2_LCD_DB4, PSD2_LCD_DB5, PSD2_LCD_DB6, PSD2_LCD_DB7 };

static void lcd_nibble(uint8_t v) {
  for (uint8_t i = 0; i < 4; i++)
    gpio_put(lcd_db[i], (v >> i) & 1);
}

static inline void lcd_set_data_mode(void)    { gpio_put(PSD2_LCD_RS, 1); }
static inline void lcd_set_command_mode(void) { gpio_put(PSD2_LCD_RS, 0); }

static void lcd_pulse_e(void) {
  gpio_put(PSD2_LCD_E, 1);
  delay_us(LCD_DELAY_US_E);
  gpio_put(PSD2_LCD_E, 0);
}

static void lcd_write(uint8_t v) {
  lcd_nibble(v >> 4);
  lcd_pulse_e();
  delay_us(LCD_DELAY_US_DATA);
  lcd_nibble(v);
  lcd_pulse_e();
  delay_us(LCD_DELAY_US_DATA);
}

void lcd_send_command(uint8_t cmd) {
  lcd_set_command_mode();
  lcd_write(cmd);
}

void lcd_locate(uint8_t x, uint8_t y) {
  if (x >= LCD_COLS) {
    x = 0;
    y++;
  }
  if (y >= LCD_LINES) {
    lcd_clear();
    y = 0;
  }
  lcd_set_command_mode();
  uint8_t start;
  switch (y) {
    case 0:  start = LCD_ADDR_LINE1; break;
    case 1:  start = LCD_ADDR_LINE2; break;
    case 2:  start = LCD_ADDR_LINE3; break;
    case 3:  start = LCD_ADDR_LINE4; break;
    default: start = 0;
  }
  lcd_write(LCD_DDRAM + start + x);
  lcd_x = x;
  lcd_y = y;
}

void lcd_clear(void) {
  lcd_send_command(0x01);
  lcd_x = lcd_y = 0;
  delay_ms(LCD_DELAY_MS_CLEAR);
}

void lcd_home(void) {
  lcd_locate(0, 0);
}

#ifdef CONFIG_MULTIDRIVE
/* Tilde als Zeichen 0 im CGRAM (das HD44780 zeigt sonst einen Pfeil) */
#define LCD_CHAR_TILDE 0

static const uint8_t lcd_tilde[8] = {
  0x00, 0x00, 0x08, 0x15, 0x02, 0x00, 0x00, 0x00
};

static bool lcd_tilde_loaded;

static void lcd_define_tilde(void) {
  lcd_send_command(0x40 | (LCD_CHAR_TILDE << 3));
  lcd_set_data_mode();
  for (uint8_t i = 0; i < sizeof(lcd_tilde); i++)
    lcd_write(lcd_tilde[i]);
  lcd_tilde_loaded = true;
}
#endif

/* Volles Kaestchen als Zeichen 1 im CGRAM: Das Menue zeichnet Balken
   (Kontrast, Helligkeit) mit 0xFF. Das ist nur beim ROM-Code A00 ein
   Kaestchen, beim A02 ("European standard font") ein y mit Trema. Ueber das
   CGRAM sieht der Balken bei beiden gleich aus. Wie die Tilde wird das
   Zeichen beim ersten Gebrauch geladen und nach dem Startlogo erneut. */
#define LCD_CHAR_BLOCK 1
static bool lcd_block_loaded;

static void lcd_define_block(void) {
  lcd_send_command(0x40 | (LCD_CHAR_BLOCK << 3));
  lcd_set_data_mode();
  for (uint8_t i = 0; i < 8; i++)
    lcd_write(0x1f);
  lcd_block_loaded = true;
}

void lcd_load_custom_chars_P(const uint8_t *glyphs) {
  uint8_t x = lcd_x, y = lcd_y;
#ifdef CONFIG_MULTIDRIVE
  lcd_tilde_loaded = false;
#endif
  lcd_block_loaded = false;
  lcd_send_command(0x40);
  lcd_set_data_mode();
  for (uint8_t i = 0; i < 64; i++)
    lcd_write(glyphs[i] & 0x1f);
  lcd_locate(x, y);
}

void lcd_cursor(bool on) {
  lcd_send_command(on ? 0x0F : 0x0C);
}

/* Leitungen erst auf low, dann als Ausgang */
void lcd_lines_init(void) {
  static const uint8_t pins[] = { PSD2_LCD_E, PSD2_LCD_RS, PSD2_LCD_DB4,
                                  PSD2_LCD_DB5, PSD2_LCD_DB6, PSD2_LCD_DB7 };
  for (uint8_t i = 0; i < sizeof(pins); i++) {
    gpio_init(pins[i]);
    gpio_put(pins[i], 0);
    gpio_set_dir(pins[i], GPIO_OUT);
  }
}

void lcd_init(void) {
  lcd_lines_init();
  delay_ms(LCD_DELAY_MS_POWERON);

  for (uint8_t pass = 0; pass < 2; pass++) {
    lcd_nibble(0x03);
    lcd_pulse_e(); delay_ms(LCD_DELAY_MS_LONG);
    lcd_pulse_e(); delay_ms(LCD_DELAY_MS_LONG);
    lcd_pulse_e(); delay_ms(LCD_DELAY_MS_LONG);
    lcd_nibble(0x02);               // 4 Bit
    lcd_pulse_e(); delay_ms(LCD_DELAY_MS_LONG);
  }

  /* WS0010: fuenf Null-Nibbles, dann 0x02 (siehe avr/lcd.c) */
  lcd_nibble(0x00);
  for (uint8_t n = 0; n < 5; n++) {
    lcd_pulse_e();
    delay_ms(LCD_DELAY_MS_LONG);
  }
  lcd_nibble(0x02);
  lcd_pulse_e();
  delay_ms(LCD_DELAY_MS_LONG);

  lcd_set_command_mode();
  lcd_write(0x28);             // 4 Bit, 2 Zeilen, 5x7
  delay_ms(LCD_DELAY_MS_SHORT);
  lcd_write(0x17);             // WS0010: Zeichenmodus, Versorgung an
  delay_ms(LCD_DELAY_MS_SHORT);
  lcd_write(0x08);             // Anzeige aus
  delay_ms(LCD_DELAY_MS_SHORT);
  lcd_clear();
  lcd_write(0x06);             // Adresse hochzaehlen, kein Schieben
  delay_ms(LCD_DELAY_MS_SHORT);
  lcd_write(0x0C);             // Anzeige an, kein Cursor
  delay_ms(LCD_DELAY_MS_SHORT);

  lcd_ready = true;
}

void lcd_putc(char c) {
  if (c == '\n') {
    lcd_x = 0;
    lcd_y++;
  }
#ifdef CONFIG_MULTIDRIVE
  if (c == '~' && !lcd_tilde_loaded)
    lcd_define_tilde();
#endif
  if ((uint8_t)c == 0xFF && !lcd_block_loaded)
    lcd_define_block();
  lcd_locate(lcd_x, lcd_y);
  if ((uint8_t)c < 8 || (uint8_t)c >= ' ') {
    lcd_set_data_mode();
#ifdef CONFIG_MULTIDRIVE
    if (c == '~') c = LCD_CHAR_TILDE;
#endif
    if ((uint8_t)c == 0xFF) c = LCD_CHAR_BLOCK;
    lcd_write(c);
    lcd_x++;
  }
}

void lcd_printf_P(const char *fmt, ...) {
  char buf[LCD_COLS * LCD_LINES + 1];
  va_list args;
  if (!lcd_ready)
    return;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  lcd_puts(buf);
}

void lcd_puts(const char *s) {
  while (*s)
    lcd_putc(*s++);
}

void lcd_puts_P(const char *s) {
  lcd_puts(s);
}

void lcd_clrlines(uint8_t from, uint8_t to) {
  lcd_locate(0, from);
  for (uint8_t i = from; i <= to; i++)
    for (uint8_t j = 0; j < LCD_COLS; j++)
      lcd_putc(' ');
}

uint8_t lcd_set_contrast(uint8_t contrast) {
  return i2c_write_register(I2C_SLAVE_ADDRESS, PWM_CONTRAST, contrast);
}

uint8_t lcd_set_brightness(uint8_t brightness) {
  return i2c_write_register(I2C_SLAVE_ADDRESS, PWM_BRIGHTNESS, 255 - brightness);
}

#endif
