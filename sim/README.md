# Simulating petSD+ and petSD2 with a PET in VICE

This directory lets you run the drive firmware on your PC, connected to a
simulated PET (xpet from VICE) over a simulated IEEE-488 bus, with the
front panel (20×4 LCD, PREV/NEXT/SELECT buttons) in your web browser:

| Drive | Firmware | Simulator |
| --- | --- | --- |
| petSD+ (original, ATmega1284P) | any NODISKEMU `.elf` for the petSD+, unchanged | `petsdsim` (simavr, cycle-exact at 8 MHz) |
| petSD2 (RP2350 / Pico 2 on the DIP40 adapter) | `firmware/petsd2-dip40-emu.bin` (prebuilt) | `psd2sim` (picoem) |

Both use the same front-panel model (`psdpanel`: HD44780 LCD, ATtiny25 for
contrast/brightness, DS3231 clock, button ladder), so the two firmwares can
be compared screen by screen. The SD card is an image file (MBR + FAT),
D64/D80/D81/D82 images on it work as on the real card.

## Quick start

```
python3 sim/petsd-sim.py --new-sd mycard.img                  # empty 64 MB card
python3 sim/petsd-sim.py --avr NODISKEMU.elf --sd mycard.img  # original petSD+
python3 sim/petsd-sim.py --pico --sd mycard.img               # petSD2
```

The PET window opens, the panel at <http://localhost:8488/> (also opened
automatically). On the PET: `directory u11`, `dload"name",u11` and so on —
**the drive is device 11**, as a petSD+ with a fresh EEPROM. Change it in the
menu or with `print#15,"u0>"chr$(8)` and it stays: settings are kept in an
EEPROM file next to the card (`mycard.eep`).

Panel: click the buttons, or use ←/→/Enter on the page (PREV/NEXT/SELECT).
Press SELECT for the menu, as on the real drive.

Options: `--model 4032` (PET model, default 8032), `--port 8490`,
`--no-browser`, `--eeprom file`, and anything after `--` goes to xpet, e.g.
`-- -warp`.

## Putting files on the card image

The image is a normal FAT16 disk with one partition starting at 1 MB:

- Linux / WSL: `mcopy -i mycard.img@@1M game.d64 prog.prg ::/` (mtools)
- macOS: `hdiutil attach mycard.img` mounts it in Finder; or mtools from
  Homebrew (`brew install mtools`) with the same command as Linux
- Windows: WSL with mtools, or OSFMount

An image of a real card (`dd`, Win32 Disk Imager) works as well.

## Building

You need: Python 3, Rust (`rustup`), a C compiler, and for the PET the VICE
fork **VICE-LRE-SIMLINK** (<https://github.com/MacGyver4B/VICE-LRE-SIMLINK>,
branch `simlink`), which adds the `-ieeesimlink` bus. Clone it next to the
simulators so the build finds `simlink.c` and `xpet`:

```
git clone https://github.com/MacGyver4B/NODISKEMU-pico.git
cd NODISKEMU-pico/sim
git clone -b simlink https://github.com/MacGyver4B/VICE-LRE-SIMLINK.git vice-lre-simlink
```

Build xpet in `vice-lre-simlink/vice` as described in its README (the
launcher finds `vice/src/xpet` there; an installed xpet from this fork in
your PATH, or `--xpet path`, works too). Then:

```
(cd psdpanel && cargo build --release)     # front panel, used by both
(cd psd2sim  && cargo build --release)     # petSD2 simulator
(cd petsdsim && make)                      # petSD+ simulator (needs simavr)
```

simavr: Debian/Ubuntu `apt install libsimavr-dev libelf-dev`; macOS
`brew tap osx-cross/avr && brew install simavr libelf`. On Windows simavr is
not available natively — use WSL (Ubuntu) for the whole setup; with WSLg the
xpet window and the browser work as usual.

The petSD2 firmware is prebuilt in `firmware/`. To build it yourself you need
the Pico SDK: `cmake -S pico -B pico/build-emu -DPSD2_EMU=1
-DPICO_BOARD=petsd2_dip40 && make -C pico/build-emu`.

To test your own NODISKEMU changes on the petSD+, build the firmware as
usual (`make CONFIG=configs/config-petSD+`) and pass the `.elf` with `--avr`.

## Comparing petSD+ and petSD2

Both simulators can run without a PET, with a key sequence in simulated
time, and log every LCD change:

```
PSD2_LCDDUMP=1 PSD_KEYS="14000:SEL:300,15500:NEXT:200" PSD_RTC_START=1795370400 \
  petsdsim/petsdsim -p -t 20000 NODISKEMU.elf card.img - 2> avr.log
PSD2_LCDDUMP=1 PSD_KEYS="7000:SEL:300,8500:NEXT:200" PSD_RTC_START=1795370400 PSD2_SOLO=13000 \
  psd2sim/target/release/psd2sim -b dip40 -s card.img firmware/petsd2-dip40-emu.bin x 2> pico.log
python3 psdpanel/tools/lcdcmp.py avr.log pico.log
```

(`PSD_KEYS`: from ms, buttons, for ms. The AVR needs about 7 s longer to its
status screen, hence the later start. `PSD_RTC_START` makes the clock
reproducible.)

## Status and limits

- Tested on Linux (Ubuntu). macOS and Windows/WSL are expected to work but
  have not been tried yet — please report what happens.
- IEEE-488 only: the petSD+ IEC port (J4) is not simulated. The petSD2's
  IEC host (relay to serial drives) is, see `../README-petSD2.md`.
- The card is always inserted and not write-protected.
- The ATtiny25 is modelled by function (contrast, brightness, IEC enable),
  the page shows contrast/brightness only approximately.
- Clock: DS3231 (the PCF8583 the firmware also probes is not modelled).
