# Simulating petSD+ and petSD2 with a PET in VICE

This directory lets you run the drive firmware on your PC, connected to a
simulated PET (xpet from VICE) over a simulated IEEE-488 bus, with the
front panel (20×4 LCD, PREV/NEXT/SELECT buttons, BUSY and DIRTY LEDs) in
your web browser:

| Drive | Firmware | Simulator |
| --- | --- | --- |
| petSD+ (original, ATmega1284P) | any NODISKEMU `.elf` for the petSD+, unchanged | `petsdsim` (simavr, cycle-exact at 16 MHz, 8 MHz in IEC mode, as on the board) |
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

### A real USB stick or SD card as the card

Instead of an image file the drive can use a real medium: a USB stick or an
SD card in a USB reader.

    python3 petsd-sim.py --list-drives
    python3 petsd-sim.py --avr --sd PETSD2

Only removable media up to 32 GB are listed at all (Windows: drive type
"Removable", the size of the whole disk counts). One of them can be used
if it is FAT formatted and its volume name starts with `PETSD` (any case;
rename it in Explorer, e.g. `PETSD`, `PETSD2`, `PETSD-GAME`). `--sd` chooses
it by 5 to 9 characters: the start of its volume name or of its volume
serial (`1A2B-3C4D`, as shown by `--list-drives` and by `vol G:`). Exactly
one medium must match, otherwise nothing is touched. Sticks cloned at the
factory may share a serial; `--new-id G:` (Linux: `--new-id /dev/sdb`) gives
one a new random serial after asking. The EEPROM settings are kept next to
the launcher as `usb-<serial>.eep`.

Windows needs administrator rights for raw disk access; the launcher asks
for them and continues in a new console window. While the simulation runs
the medium is locked and disappears from Explorer; close the simulation
before pulling it. A medium pulled during the simulation is noticed within
half a second: the drive reports it, opens its card-detect switch and the
card stops answering (the PET gets a read error); restart the simulation to
use it again. Linux: the medium must not be mounted, and you need write
access to the device (`sudo` or group `disk`).

The green BUSY LED is lit while channels are open, the red DIRTY LED shows
unwritten data or blinks on an error, as on the board. While the PET
accesses a drive, the `#` in front of its address on the status screen
becomes a full block, until 1 s after the last access (firmware of this branch; the official NODISKEMU
shows it normally).

Panel: click the buttons, or use ←/→/Enter on the page (PREV/NEXT/SELECT).
Press SELECT for the menu, as on the real drive.

The drive needs about 6 seconds after start until it answers (SD card and
display initialisation, the logo); the PET is ready earlier. A command typed
right away gives `?DEVICE NOT PRESENT`, just as on real hardware switched on
together with the PET.

Options: `--model 4032` (PET model, default 8032), `--port 8490`,
`--no-browser`, `--eeprom file`, `--vcd DIR` (bus captures, see below), and
anything after `--` goes to xpet, e.g. `-- -warp`.

## Putting files on the card image

The image is a normal FAT16 disk with one partition starting at 1 MB:

- Linux / WSL: `mcopy -i mycard.img@@1M game.d64 prog.prg ::/` (mtools)
- macOS: `hdiutil attach mycard.img` mounts it in Finder; or mtools from
  Homebrew (`brew install mtools`) with the same command as Linux
- Windows: WSL with mtools, or OSFMount

An image of a real card (`dd`, Win32 Disk Imager) works as well.

## Building

You need: Python 3, Rust (`rustup`), a C compiler, for the petSD+ the AVR
toolchain (simavr is built from the bundled sources and needs the avr-libc
headers), and for the PET the VICE
fork **VICE-LRE-SIMLINK** (<https://github.com/MacGyver4B/VICE-LRE-SIMLINK>,
branch `simlink`), which adds the `-ieeesimlink` bus. Clone it next to the
simulators so the build finds `simlink.c` and `xpet`:

```
git clone --recursive https://github.com/MacGyver4B/NODISKEMU-pico.git
cd NODISKEMU-pico/sim
git clone -b simlink https://github.com/MacGyver4B/VICE-LRE-SIMLINK.git vice-lre-simlink
```

(Cloned without `--recursive`? `git submodule update --init sim/simavr`.)

Build xpet in `vice-lre-simlink/vice` as described in its README (the
launcher finds `vice/src/xpet` there; an installed xpet from this fork in
your PATH, or `--xpet path`, works too). Run the full `make` there: `make -C
src xpet` alone misses `libsimlink.a`. On Debian/Raspberry Pi OS configure
also needs `libevdev-dev`. Then:

```
(cd psdpanel && cargo build --release)     # front panel, used by both
(cd psd2sim  && cargo build --release)     # petSD2 simulator
(cd petsdsim && make)                      # petSD+ simulator, builds simavr 1.8 first
```

petsdsim uses simavr 1.8 from the submodule `sim/simavr`. To build it you
need the AVR toolchain and libelf: Debian/Ubuntu/Raspberry Pi OS
`apt install gcc-avr avr-libc libelf-dev`; macOS `brew tap osx-cross/avr &&
brew install avr-gcc libelf`. `make SIMAVR=system` uses the simavr of your
distribution instead (Debian/Ubuntu ship 1.6, `libsimavr-dev`); petsdsim
works around the known 1.6 bugs, but 1.8 is preferred.

### Windows

No WSL needed. The GitHub workflow "sim - Windows build" (Actions tab of
this repo, started by hand) builds `petsdsim.exe`, `psd2sim.exe` and the
petSD+ firmware with MSYS2 and packs them with the launcher into the
artifact `petsd-sim-windows`. xpet comes from the workflow "simlink -
Windows GTK3 build" of VICE-LRE-SIMLINK (artifact `vice-gtk3-win64-simlink`):
unpack the VICE zip inside it next to `petsd-sim.py`; the folder
`GTK3VICE-...` with `bin\xpet.exe` is found automatically (as is a folder
`vice`), otherwise pass `--xpet`.
Then, with Python 3 for Windows:

```
py petsd-sim.py --new-sd card.img
py petsd-sim.py --avr  --sd card.img      petSD+ (firmware included)
py petsd-sim.py --pico --sd card.img      petSD2 (firmware included)
```

To build it yourself: MSYS2 shell UCRT64 with the packages listed in the
workflow file, then the same `make CC=gcc` / `cargo build --release` steps.

The petSD2 firmware is prebuilt in `firmware/`. To build it yourself you need
the Pico SDK: `cmake -S pico -B pico/build-emu -DPSD2_EMU=1
-DPICO_BOARD=petsd2_dip40 && make -C pico/build-emu`.

To test your own NODISKEMU changes on the petSD+, build the firmware as
usual (`make CONFIG=configs/config-petSD+`) and pass the `.elf` with `--avr`.

## Comparing petSD+ and petSD2

Both simulators can run without a PET, with a key sequence in simulated
time, and log every LCD change:

```
PSD2_LCDDUMP=1 PSD_KEYS="7000:SEL:300,8500:NEXT:200" PSD_RTC_START=1795370400 \
  petsdsim/petsdsim -p -t 13000 NODISKEMU.elf card.img - 2> avr.log
PSD2_LCDDUMP=1 PSD_KEYS="7000:SEL:300,8500:NEXT:200" PSD_RTC_START=1795370400 PSD2_SOLO=13000 \
  psd2sim/target/release/psd2sim -b dip40 -s card.img firmware/petsd2-dip40-emu.bin x 2> pico.log
python3 psdpanel/tools/lcdcmp.py avr.log pico.log
```

(`PSD_KEYS`: from ms, buttons, for ms. Both reach their status screen after
about 6.3 s. `PSD_RTC_START` makes the clock reproducible.)

## Bus captures for GTKWave (petSD+)

With `--vcd DIR` the petSD+ simulator writes one VCD file per bus transfer
into DIR, to be opened with GTKWave (<https://gtkwave.sourceforge.net/>,
also as a Windows build). A capture starts with the first ATN after a quiet
bus (with 1 ms of history), ends right after a CLOSE, otherwise after 250 ms
without a change (`petsdsim -G ms`). The file name tells what happened:

```
20261123-111040-793_d11_DIR.vcd           DIRECTORY / LOAD"$"
20261123-111044-858_d11_CMD_i0.vcd        command channel 15
20261123-111044-931_d11_LOAD_0_f07.seq.vcd
20261123-111100-002_d11_TALK2.vcd         reading on without OPEN (GET#)
```

Signals: `pet.*` and `petsd.*` are what each side drives, `bus.*` the
resulting level (electrical: 0 = pulled low), `petsd.TE` the 75160/75161
direction, `petsd.SD_CS` the card select and `petsd.ATN_ISR` the ATN
interrupt routine running. `bus.BYTE` shows every transferred byte decoded
(`LISTEN_11`, `OPEN_15`, `49_'I'`, `30_'0'_EOI`, ...). Times are simulated
nanoseconds. Examples are in `petsdsim/examples/`.

With the environment variable `PSD_HANG=1` petsdsim reports when the
firmware has accepted a byte (NRFD held low, NDAC released) but has not
finished the handshake 1 ms after DAV went high again. The line gives the
AVR program counter and the top of the stack (`petsdsim ... ms: HAENGER PC
...`); `avr-addr2line -f -e NODISKEMU.elf 0x<PC>` names the function. This
found a dropped UNLISTEN in firmware older than 2026-11-22. Lines during the
first 7 s (boot) and while ATN is low are normal.

## Status and limits

- Tested on Linux (Ubuntu) and by Torsten (tuti) on a Raspberry Pi 5
  (Debian 12). macOS and the native Windows build are expected to work but have not been
  tried yet — please report what happens.
- Wayland: if the PET window stays black, start with `GDK_BACKEND=x11`.
- `petsdsim -l us` (lookahead, default 2 µs) delays every reaction of the
  drive by that time. Much more than the default breaks the ATN handshake
  (`-l 20` gives `?DEVICE NOT PRESENT`).
- IEEE-488 only: the petSD+ IEC port (J4) is not simulated. The petSD2's
  IEC host (relay to serial drives) is, see `../README-petSD2.md`.
- The card is always inserted and not write-protected.
- The ATtiny25 is modelled by function (contrast, brightness, IEC enable),
  the page shows contrast/brightness only approximately.
- Clock: DS3231 (the PCF8583 the firmware also probes is not modelled).
