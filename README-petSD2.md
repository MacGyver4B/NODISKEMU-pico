# NODISKEMU-pico – petSD2+IEC

Fork von [NODISKEMU](https://github.com/nils-eilers/NODISKEMU) (Stand 1.2.0) mit einer
Portierung auf den RP2350B (Olimex RP2350-PICO2-XL/XXL oder RP2350-PICO2-BB48/BB48R) und
einem Relais vom IEEE-488-Bus des PET zu seriellen IEC-Laufwerken (1541, 1571, 1581).
Am IEEE-Bus arbeitet es wie der petSD+ mit SN75160/SN75161.

Der AVR-Build (petSD+, Makefile) bleibt unverändert. Alles Neue steckt in eigenen
Dateien; in `src/ieee.c` sind die Änderungen per `HAVE_ARCH_IEEE_LL` bzw.
`CONFIG_IEC_RELAY` abgegrenzt.

## Inhalt

| Pfad | Inhalt |
|---|---|
| `src/rp2350/` | Arch-Schicht: IEEE-Leitungen über SN75160/75161 (SIO), ATN per PIO-IRQ, SD an SPI1, UART0, SysTick, DWT-Zeitbasis, IEC-Pins, AR6-Senden per PIO |
| `src/iecrelay.c/.h` | Relais IEEE → IEC: Namen/Befehle/Daten weiterreichen, Kanal vorab holen, AR6-Schnelllader (LOAD 1541/1571/1581, SAVE 1541/1571 einseitig) |
| `configs/config-petSD2` | NODISKEMU-Konfiguration |
| `pico/` | CMake-Build mit dem pico-sdk, Board-Dateien in `pico/boards/` |
| `sim/psd2sim/` | Brücke: Firmware in picoem ([Fork mit Korrekturen](https://github.com/MacGyver4B/picoem/tree/rp2350b-fixes), Zweig `rp2350b-fixes`) an VICE (simlink, IEEE + IEC), Modell der SN75160/75161, SD-Kartenmodell, Testskripte |

## Belegung (GPIO)

IEEE-488 wie beim petSD+ über SN75160 (Daten) und SN75161 (Steuerung), DC fest
auf high (Gerät), PE des 75160 auf low (Passiv-Pull-up), ein gemeinsames TE.
Alle 7516x-Leitungen liegen auf GPIOs unter 40: Der RP2350 ist ab Stepping A4
5-V-tolerant, die ADC-Pins (GPIO 40–47) aber nicht. IOVDD muss stehen, bevor
die 7516x Pegel liefern.

| GPIO | Funktion |
|---|---|
| 16–23 | IEEE D1–D8 (75160) |
| 26 | TE (75160 + 75161, high = sprechen) |
| 27, 28, 29 | ATN, IFC, REN – immer Eingang |
| 30, 31 | EOI, DAV – Ausgang beim Sprechen, sonst Eingang |
| 32, 33 | NRFD, NDAC – Ausgang beim Hören, sonst Eingang |
| 34 | SRQ – Ausgang (frei = high) |
| 39–42 | IEC CLK, DATA, ATN, SRQ – Eingänge über 74LVC14A (invertiert) |
| 43–47 | IEC CLK, DATA, ATN, SRQ, RESET – Ausgänge über 74LS07 |
| 9/10/11/24 | microSD CS, CLK, CMD, DAT0 (SPI1; XXL und BB48R auf dem Board, XL und BB48 auf der Trägerplatine) |
| 8 | PSRAM CS (XXL, BB48R; nicht benutzt) |
| 0/1 | UART0 Debug |
| 25 | LED (auf dem Board) |

Das ist die Vorgabe in `src/rp2350/arch-config.h` (`PSD2_*`, SD-Karte, PIO-Fenster)
und `src/rp2350/iec-host.h` (`IEC_*`). Eine Board-Datei kann jeden Pin
überschreiben. Beim Übersetzen wird geprüft, ob die Belegung zum Code passt:
D1–D8 zusammenhängend in GPIO 0–31 oder 32–39, alle 7516x-Leitungen
unter 40, IEC-Pins beliebig in 0–47 (DATA_O direkt hinter CLK_O), alles im
PIO-Fenster, SD an SCK/TX/RX desselben SPI-Blocks, I²C an SDA/SCL desselben Blocks.
IEEE und IEC zusammen brauchen den RP2350B; ein RP2350A (GPIO 0–29) reicht nicht.

| Board-Datei | Board | Flash |
|---|---|---|
| `olimex_rp2350_pico2_xxl` (Vorgabe) | Olimex RP2350-PICO2-XXL (mit PSRAM, microSD) | 16 MB |
| `olimex_rp2350_pico2_xl` | Olimex RP2350-PICO2-XL | 2 MB |
| `olimex_rp2350_pico2_bb48` | Olimex RP2350-PICO2-BB48 (DIL 0,6") | 2 MB |
| `olimex_rp2350_pico2_bb48r` | Olimex RP2350-PICO2-BB48R (DIL 0,6", mit PSRAM, microSD) | 16 MB |
| `petsd2_dip40` | petSD2: XXL auf dem Adapter im DIP40-Sockel des petSD+ Rev. 2.4 | 16 MB |

### petSD2 im DIP40-Sockel (`petsd2_dip40`)

Belegung nach `pico/boards/petsd2_dip40.h` (Herleitung im docufolder,
`petsd2/adapter-dip40-pinout.md`), gewählt nach der Platine: Das Olimex sitzt
1,27 mm gegen H1 versetzt, jede Leitung geht zum GPIO neben ihrem H1-Pin.
D1–D8 an GPIO 32–39, NRFD 24, NDAC 25, DAV 26, EOI 27, ATN 28, TE 29, IFC 12
(REN und SRQ beim petSD+ nicht angeschlossen), LEDs dirty/busy an 10/11,
SD-Schacht des petSD+ an SPI0 (CS 3, MOSI 19, MISO 20, SCK 22, Kartenschalter 15,
Schreibschutz 9), I²C1 an 30/31 (ATtiny25 0x4C, DS3231 0xD0), LCD 20×4 an
16/17/18/2 (DB4–DB7), 13 (RS), 14 (E), Tasten an ADC0 (GPIO 40) hinter dem Teiler
56k/100k, IEC-Host aus 41–45 (CLK, DATA, ATN, SRQ, RESET), ein 46/47/21/23
(CLK, DATA, ATN, SRQ). Alle 5-V-Leitungen liegen unter GPIO 40, alle PIO-Leitungen
im Fenster 16–47. Die Firmware erlaubt jede Leitung im unteren oder oberen
SIO-Register; die Prüfungen in `arch-config.h` und `iec-host.h` melden ungültige
Belegungen schon beim Übersetzen.

Die Bedienung entspricht dem petSD+ (AVR): Startbild, Statusanzeige, Menü mit
PREV/NEXT/SEL, Geräteadresse über die Tasten, wenn das Menü abgeschaltet ist,
PREV beim Einschalten = Hardwaretest, NEXT = Kontrast einstellen. Benutzt werden
`menu.c`, `menu-drives.c`, `rtc.c`, `ds1307-3231.c` wie beim AVR; neu für den RP2350
sind nur die Hardwareschichten in `src/rp2350/` (`lcd.c`, `analog-buttons.c`,
`i2c.c`/`i2c_hw.c`, `diagnose.c`, Entprellung in `arch-timer.c`). Jede Funktion
lässt sich in der Konfiguration abschalten (`CONFIG_ONBOARD_DISPLAY`,
`CONFIG_RTC_DSRTC`); ohne sie entsteht dieselbe Firmware wie für die anderen Boards.
Die Tastenschwellen sind die des AVR: Der 12-Bit-Wert hinter dem Teiler wird auf
die 10-Bit-Skala des AVR (Referenz 5 V) umgerechnet.

Zeichensatz des LCD-Moduls: ROM-Code A00 (Vorgabe) oder A02 mit
`CONFIG_LCD_ROM_A02=y`; was dem Modul fehlt (Tilde bei A00, Kästchen für die
Menübalken bei A02), legt die Firmware ins CGRAM.

Offen: Einstellungen aus dem Menü gehen beim Neustart verloren (das EEPROM liegt
noch im RAM, siehe `src/rp2350/arch-eeprom.h`). Die Tastenmessung ist nicht mehr ratiometrisch
(ADC gegen 3,3 V statt gegen die 5 V der Leiter); die Fenster des AVR vertragen nur
etwa ±2 % auf der 5-V-Schiene (Details und Abhilfe im docufolder).

Beim Start meldet die Firmware die Chip-Revision am Debug-UART (Revision ≤ 2 = A2).

## Bauen

```sh
export PICO_SDK_PATH=/pfad/zum/pico-sdk        # getestet mit 2.1.1
cd pico
mkdir build && cd build && cmake .. && make    # petsd2.uf2 für die Hardware
# anderes Board: cmake -DPICO_BOARD=olimex_rp2350_pico2_bb48 ..
# petSD2 im petSD+: cmake -DPICO_BOARD=petsd2_dip40 ..   (Konfiguration config-petSD2-dip40)
```

Für die Simulation: `cmake -DPSD2_EMU=1 ..` in `pico/build-emu`. Diese Variante
umgeht Bootrom-Funktionen, die picoem nicht nachbildet, und spricht SD-Karte und
Debugausgabe über ein Postfach im RAM an (`src/rp2350/emu.h`).

## AR6-Schnelllader

Der Laufwerkscode des Action Replay 6 ist nicht Teil der Firmware. Die Firmware liest
beim Start `AR6.CRT` (Action Replay V6.0, CRT-Format) aus dem Wurzelverzeichnis der
SD-Karte. Fehlt die Datei, arbeitet das Relais mit dem normalen seriellen Protokoll.

## Simulation und Tests

picoem holt `cargo` selbst aus dem Fork (Zweig `rp2350b-fixes`: CPS-Bits, SIO
GPIO_HI_OUT/OE, Gehäuseauswahl RP2350B mit neun ADC-Kanälen). Ein Bootrom-Abbild
ist nicht nötig, weil die Emulator-Firmware ihn umgeht; `PICOEM_BOOTROM` lädt bei Bedarf trotzdem eins.
Neben `sim/` wird nur `vice-lre-simlink/` gebraucht (VICE-Fork, Zweig `simlink`,
gebaut). Pfade lassen sich in `sim/psd2sim/env.sh` überschreiben.

```sh
cd sim/psd2sim && cargo build --release
AR6CRT=/pfad/ar6.crt ./test/mkwork.sh          # Testdaten nach $WORK (/tmp/psd)
./test/run-tests.sh                             # 35 DOS-Tests an der SD-Karte
./test/run-relay-tests.sh                       # Relais: std1541 ar1541 ar1571 ar1581 absent
./test/run-relay-tests.sh ss1571 ds1571 std1581 # weitere Fälle
```

**petSD2 mit Bedienfeld** (Firmware aus `cmake -DPSD2_EMU=1 -DPICO_BOARD=petsd2_dip40`):
`psd2sim -b dip40` bildet LCD, Tasten, ATtiny25 und DS3231 nach und zeigt beides
auf einer Webseite (`-w port`, Vorgabe 8488): `http://localhost:8488/`. Tasten per
Maus, Pfeil links/rechts/Eingabe oder Skript
(`curl -X POST 'localhost:8488/key?k=SEL&ms=200'`, `&down=1`/`&down=0` zum Halten),
die Anzeige als Text unter `/text`. `-k PREV` hält Tasten ab dem Start (Hardwaretest),
`-r A02` zeigt den europäischen Zeichensatz (Vorgabe A00, passend zu
`CONFIG_LCD_ROM_A02` der Firmware), `PSD2_LCDDUMP=1` schreibt jede Änderung der Anzeige ins Protokoll, `PSD2_TZ` setzt
die Zeitzone der Uhr (Stunden, Vorgabe 1). Weil das Startbild 5 s steht, braucht
xpet für die Tests eine Tastaturverzögerung:

```sh
export PSD2_BOARD=dip40 FW=../../pico/build-emu-dip40/petsd2.bin XARGS="-keybuf-delay 8000000"
./test/run-tests.sh && ./test/run-relay-tests.sh
```

Ergebnisse (Stand 19.11.2026, PET 4032 in xpet, 23.104 Bytes, alles bytegleich):

| Fall | LOAD | SAVE |
|---|---|---|
| SD-Karte FAT/D64/D82 | ~4.950 B/s | ~5.100 B/s |
| 1541 AR6 | 4.628 B/s (IEC) | AR6 |
| 1571 einseitig, AR6 im 1541-Modus | 4.183 B/s (IEC) | AR6 |
| 1571 doppelseitig | AR6 bis Spur 35, dann normal | normal |
| 1581 AR6 | 6.143 B/s (IEC) | normal |

Bekannte Lücken: Burst (1571/1581), Konfiguration im Flash; REL-Dateien in Images
fehlen schon in NODISKEMU selbst.

## Lizenz

GPL-2.0 wie NODISKEMU (siehe `COPYING`).
