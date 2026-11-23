#!/usr/bin/env python3
"""petsd-sim.py - run a simulated petSD+ (ATmega1284P) or petSD2 (RP2350)
together with a PET in VICE (xpet), with the front panel (LCD, buttons) in
the web browser.

  petsd-sim.py --avr NODISKEMU.elf --sd card.img        original petSD+
  petsd-sim.py --pico [petsd2.bin] --sd card.img        petSD2 (Pico 2)
  petsd-sim.py --new-sd card.img                        create an empty card

Both drives start as device 11 (fresh EEPROM, as delivered). Settings
saved in the menu or with U0>... persist in an EEPROM file next to the
card image (card.eep). The panel is at http://localhost:8488/.

Runs on Linux, macOS and Windows (the AVR simulator needs simavr, i.e.
Linux, macOS or WSL). Paths to the programs can be given with --xpet,
--petsdsim and --psd2sim or the environment variables XPET, PETSDSIM and
PSD2SIM.
"""
import argparse
import gzip
import os
import platform
import re
import shutil
import subprocess
import sys
import threading
import time
import webbrowser

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = '.exe' if platform.system() == 'Windows' else ''


def find(name, env, candidates):
    """first existing program: environment, candidates, then PATH"""
    if os.environ.get(env):
        return os.environ[env]
    for c in candidates:
        if os.path.isfile(c + EXE):
            return c + EXE
    return shutil.which(name)


def new_sd(path):
    if os.path.exists(path):
        sys.exit(f'{path} exists, not overwritten')
    with gzip.open(os.path.join(HERE, 'sd-empty.img.gz'), 'rb') as src, open(path, 'wb') as dst:
        shutil.copyfileobj(src, dst)
    print(f'{path}: empty 64 MB card (FAT16, label PETSD)')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group()
    g.add_argument('--avr', metavar='ELF', help='petSD+ firmware (NODISKEMU .elf for ATmega1284P)')
    g.add_argument('--pico', metavar='BIN', nargs='?', const='',
                   help='petSD2 firmware built with -DPSD2_EMU=1 (default: firmware/petsd2-dip40-emu.bin)')
    ap.add_argument('--sd', metavar='IMG', help='SD card image (MBR + FAT)')
    ap.add_argument('--new-sd', metavar='IMG', help='create an empty card image and exit')
    ap.add_argument('--vcd', metavar='DIR', help='write a GTKWave VCD file per bus transfer into DIR (petSD+ only)')
    ap.add_argument('--eeprom', metavar='FILE', help='EEPROM file (default: card image with .eep)')
    ap.add_argument('--model', default='8032', help='PET model for xpet (default 8032)')
    ap.add_argument('--port', type=int, default=8488, help='front panel web port (default 8488)')
    ap.add_argument('--no-browser', action='store_true', help='do not open the web browser')
    ap.add_argument('--xpet', help='xpet program (VICE-LRE-SIMLINK)')
    ap.add_argument('--petsdsim', help='petsdsim program')
    ap.add_argument('--psd2sim', help='psd2sim program')
    ap.add_argument('--bus', default=f'petsd{os.getpid()}', help='simlink bus name')
    ap.add_argument('xpet_args', nargs=argparse.REMAINDER, help='further xpet options after --')
    a = ap.parse_args()

    if a.new_sd:
        new_sd(a.new_sd)
        return
    if a.avr is None and a.pico is None:
        ap.error('choose --avr ELF or --pico [BIN]')
    if not a.sd:
        ap.error('--sd IMG is required (create one with --new-sd IMG)')
    eep = a.eeprom or os.path.splitext(a.sd)[0] + '.eep'

    xpet = a.xpet or find('xpet', 'XPET', [os.path.join(HERE, 'vice-lre-simlink', 'vice', 'src', 'xpet')])
    if not xpet:
        sys.exit('xpet not found (build VICE-LRE-SIMLINK, then use --xpet or XPET=...)')

    if a.avr is not None:
        emu = a.petsdsim or find('petsdsim', 'PETSDSIM', [os.path.join(HERE, 'petsdsim', 'petsdsim')])
        if not emu:
            sys.exit('petsdsim not found (cd sim/petsdsim && make; on Windows use WSL)')
        cmd = [emu, '-w', str(a.port), '-e', eep]
        if a.vcd:
            os.makedirs(a.vcd, exist_ok=True)
            cmd += ['-g', a.vcd]
        cmd += [a.avr, a.sd, a.bus]
    else:
        fw = a.pico or os.path.join(HERE, 'firmware', 'petsd2-dip40-emu.bin')
        emu = a.psd2sim or find('psd2sim', 'PSD2SIM', [os.path.join(HERE, 'psd2sim', 'target', 'release', 'psd2sim')])
        if not emu:
            sys.exit('psd2sim not found (cd sim/psd2sim && cargo build --release)')
        os.environ['PSD_EEPROM'] = '24c32'          # EEPROM on the adapter pad
        os.environ['PSD_EEPROM_FILE'] = eep
        if a.vcd:
            print('note : --vcd is only available for the petSD+ (--avr) so far')
        cmd = [emu, '-b', 'dip40', '-w', str(a.port), '-s', a.sd, fw, a.bus]

    print('drive :', ' '.join(cmd))
    drive = subprocess.Popen(cmd, stderr=subprocess.PIPE, text=True, errors='replace', bufsize=1)
    attached = threading.Event()
    ready = re.compile(r'^(petsdsim: .*, Bus |psd2sim: .* an )')

    def relay():                                 # pass drive messages through
        for line in drive.stderr:
            sys.stderr.write(line)
            if ready.match(line):
                attached.set()
        attached.set()                           # drive ended
    threading.Thread(target=relay, daemon=True).start()
    # start the PET only once the drive is on the bus, otherwise the first
    # access fails with ?DEVICE NOT PRESENT on a busy machine
    if not attached.wait(15):
        print('note : drive did not report being on the bus within 15 s, starting the PET anyway')
    if drive.poll() is not None:
        sys.exit(f'drive exited with code {drive.returncode}')
    xcmd = [xpet, '-model', a.model, '-drive8type', '0', '+busdevice8', '-ieeesimlink', a.bus]
    vdata = os.path.join(os.path.dirname(xpet), '..', 'data')
    if os.path.isdir(vdata):                     # VICE run from its build tree
        xcmd[1:1] = ['-directory', os.path.normpath(vdata)]
    xcmd += [x for x in a.xpet_args if x != '--']
    print('PET   :', ' '.join(xcmd))
    print(f'panel : http://localhost:{a.port}/   (drive: device 11, EEPROM {eep})')
    if not a.no_browser:
        time.sleep(1.0)
        webbrowser.open(f'http://localhost:{a.port}/')
    try:
        subprocess.call(xcmd)
    finally:
        drive.terminate()
        try:
            drive.wait(5)
        except subprocess.TimeoutExpired:
            drive.kill()


if __name__ == '__main__':
    main()
