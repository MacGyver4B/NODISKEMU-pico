#!/usr/bin/env python3
"""petsd-sim.py - run a simulated petSD+ (ATmega1284P) or petSD2 (RP2350)
together with a PET in VICE (xpet), with the front panel (LCD, buttons) in
the web browser.

  petsd-sim.py --avr [NODISKEMU.elf] --sd card.img      original petSD+
  petsd-sim.py --pico [petsd2.bin] --sd card.img        petSD2 (Pico 2)
  petsd-sim.py --new-sd card.img                        create an empty card
  petsd-sim.py --list-drives                            USB sticks / SD cards usable as card
  petsd-sim.py --avr --sd PETSD2                        use such a stick (name or volume serial)

Both drives start as device 11 (fresh EEPROM, as delivered). Settings
saved in the menu or with U0>... persist in an EEPROM file next to the
card image (card.eep). The panel is at http://localhost:8488/.

Instead of an image file, --sd can name a real USB stick or SD card (in a
USB reader). Only removable media up to 32 GB are considered at all; such a
medium can be used if it is FAT formatted and its volume name starts with
PETSD (any case). It is chosen by 5 to 9 characters: the start of its
volume name or of its volume serial number (e.g. PETSD2 or 1A2B-3C4D), as
shown by --list-drives. Exactly one medium must match. Windows needs
administrator rights for this (the launcher asks for them) and keeps the
medium away from Explorer while the simulation runs.

Runs on Linux, macOS and Windows (native build from the GitHub workflow
"sim - Windows build", see README). Paths to the programs can be given with --xpet,
--petsdsim and --psd2sim or the environment variables XPET, PETSDSIM and
PSD2SIM.
"""
import argparse
import glob
import gzip
import os
import platform
import re
import signal
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


# ----------------------------------------------------------------------------
# Real USB sticks / SD cards as the drive's card
# ----------------------------------------------------------------------------
MAX_BYTES = 32 * 1024 ** 3          # "up to 32 GB": SDHC and sticks sold as 32 GB


class Medium:
    def __init__(self, dev, size, name, label, serial, fs, vol, letter=None, mount=None):
        self.dev, self.size, self.name = dev, size, name
        self.label, self.serial, self.fs = label or '', serial or '', fs or ''
        self.vol, self.letter, self.mount = vol, letter, mount
        self.why = None                       # reason it cannot be used

    def check(self, nvols):
        if nvols != 1:
            self.why = f'{nvols} volumes (needs exactly one)'
        elif self.fs.upper() not in ('FAT', 'FAT12', 'FAT16', 'FAT32', 'VFAT'):
            self.why = f'file system {self.fs or "unknown"}, not FAT'
        elif not self.label.upper().startswith('PETSD'):
            self.why = 'volume name does not start with PETSD'
        return self


def _win_media():
    """removable drive letters (GetDriveTypeW) grouped by physical disk; the
    size is that of the whole disk, so a small partition of a big disk never
    counts as small. Works without administrator rights."""
    import ctypes
    from ctypes import wintypes
    k = ctypes.WinDLL('kernel32', use_last_error=True)
    k.CreateFileW.restype = wintypes.HANDLE
    bad = wintypes.HANDLE(-1).value

    def ioctl(path, code, inp=None, outsize=1024):
        h = k.CreateFileW(path, 0, 3, None, 3, 0, None)      # no access rights needed
        if h in (None, bad):
            return None
        out = ctypes.create_string_buffer(outsize)
        n = wintypes.DWORD()
        ok = k.DeviceIoControl(h, code, inp, len(inp) if inp else 0, out, outsize, ctypes.byref(n), None)
        k.CloseHandle(h)
        return out.raw[:n.value] if ok else None

    disks = {}
    mask = k.GetLogicalDrives()
    for i in range(26):
        if not mask >> i & 1:
            continue
        letter = chr(65 + i)
        if k.GetDriveTypeW(f'{letter}:\\') != 2:              # DRIVE_REMOVABLE only
            continue
        num = ioctl(rf'\\.\{letter}:', 0x2D1080)               # IOCTL_STORAGE_GET_DEVICE_NUMBER
        if not num:
            continue                                             # e.g. empty card reader slot
        disks.setdefault(int.from_bytes(num[4:8], 'little'), []).append(letter)
    res = []
    for n, letters in sorted(disks.items()):
        dev = rf'\\.\PhysicalDrive{n}'
        geo = ioctl(dev, 0x700A0)                               # IOCTL_DISK_GET_DRIVE_GEOMETRY_EX
        size = int.from_bytes(geo[24:32], 'little') if geo and len(geo) >= 32 else 0
        if not 0 < size <= MAX_BYTES:
            continue
        name = ''
        d = ioctl(dev, 0x2D1400, bytes(12))                    # IOCTL_STORAGE_QUERY_PROPERTY
        if d and len(d) >= 20:
            parts = []
            for off in (12, 16):                                # vendor, product
                o = int.from_bytes(d[off:off + 4], 'little')
                if 0 < o < len(d):
                    parts.append(d[o:].split(b'\0')[0].decode('ascii', 'replace').strip())
            name = ' '.join(p for p in parts if p)
        letter = letters[0]
        lb, fb = ctypes.create_unicode_buffer(64), ctypes.create_unicode_buffer(64)
        sn = wintypes.DWORD()
        label = serial = fs = ''
        if k.GetVolumeInformationW(f'{letter}:\\', lb, 64, ctypes.byref(sn), None, None, fb, 64):
            label, fs = lb.value, fb.value
            serial = f'{sn.value >> 16:04X}-{sn.value & 0xFFFF:04X}'
        m = Medium(dev, size, name, label, serial, fs, rf'\\.\{letter}:', letter=letter)
        res.append(m.check(len(letters)))
    return res


def _linux_media():
    import json
    out = subprocess.run(['lsblk', '-J', '-b', '-o',
                          'NAME,PATH,SIZE,RM,HOTPLUG,TRAN,TYPE,FSTYPE,LABEL,UUID,MOUNTPOINT,MODEL'],
                         capture_output=True, text=True).stdout
    loops = os.environ.get('PETSD_LOOP_AS_USB')     # tests: loop devices count as sticks
    res = []
    for d in json.loads(out or '{"blockdevices":[]}')['blockdevices']:
        removable = d.get('rm') or d.get('hotplug') or d.get('tran') in ('usb', 'mmc')
        if loops and d.get('type') == 'loop':
            removable = True
        elif d.get('type') != 'disk':
            continue
        if not removable or not 0 < int(d['size'] or 0) <= MAX_BYTES:
            continue
        vols = [c for c in d.get('children') or [] if c.get('fstype')] or ([d] if d.get('fstype') else [])
        if not vols:                               # no udev data (containers): ask blkid
            for c in (d.get('children') or []) + [d]:
                r = subprocess.run(['blkid', '-p', '-o', 'export', c['path']], capture_output=True, text=True)
                kv = dict(x.split('=', 1) for x in r.stdout.splitlines() if '=' in x)
                if kv.get('TYPE'):
                    vols = [dict(c, fstype=kv['TYPE'], label=kv.get('LABEL'), uuid=kv.get('UUID'))]
                    break
        v = vols[0] if vols else {}
        m = Medium(d['path'], int(d['size']), (d.get('model') or '').strip(), v.get('label'),
                   (v.get('uuid') or '').upper(), v.get('fstype'), v.get('path'), mount=v.get('mountpoint'))
        res.append(m.check(len(vols)))
    return res


def media():
    system = platform.system()
    if system == 'Windows':
        return _win_media()
    if system == 'Linux':
        return _linux_media()
    sys.exit('real sticks/cards are supported on Windows and Linux only')


def list_drives():
    ms = media()
    if not ms:
        print('no removable medium up to 32 GB found')
        return
    print(f'{"medium":<22}{"size":>9}  {"name":<12}{"serial":<11}{"fs":<7}')
    for m in ms:
        where = f'{m.letter}:  ' if m.letter else ''
        print(f'{m.dev:<22}{m.size / 1e9:>7.1f}GB  {m.label:<12}{m.serial:<11}{m.fs:<7}'
              f'{where}{m.name}')
        if m.why:
            print(f'{"":<22}  -> not usable: {m.why}')
    serials = [m.serial for m in ms if m.serial and not m.why]
    if len(serials) != len(set(serials)):
        print('note: some media share a volume serial (cloned at the factory); '
              'give them different names or a new serial with --new-id')


def choose(key, ms=None):
    """the one usable medium whose name or serial starts with key (5..9 chars)"""
    if not 5 <= len(key) <= 9:
        sys.exit(f'{key!r}: a medium is chosen by 5 to 9 characters of its name or volume serial')
    ms = [m for m in (ms if ms is not None else media()) if not m.why]
    k = key.upper()
    hit = [m for m in ms if k in (m.label.upper(), m.serial)] or \
          [m for m in ms if m.label.upper().startswith(k) or m.serial.startswith(k)]
    if not hit:
        sys.exit(f'{key!r}: no usable medium found (see --list-drives)')
    if len(hit) > 1:
        sys.exit(f'{key!r} is ambiguous: ' + ', '.join(f'{m.label} {m.serial} ({m.dev})' for m in hit))
    return hit[0]


def win_admin_or_relaunch():
    """Windows: raw disk access needs administrator rights - restart elevated"""
    import ctypes
    if ctypes.windll.shell32.IsUserAnAdmin():
        return
    args = [os.path.abspath(__file__), '--cwd', os.getcwd(), '--pause'] + sys.argv[1:]
    params = subprocess.list2cmdline(args)
    print('asking for administrator rights (a new console window opens)')
    if ctypes.windll.shell32.ShellExecuteW(None, 'runas', sys.executable, params, os.getcwd(), 1) <= 32:
        sys.exit('administrator rights were not granted')
    sys.exit(0)


class WinVolumeLock:
    """keep Windows (Explorer, file system) off the volume while it is in use"""
    LOCK, UNLOCK, DISMOUNT = 0x90018, 0x9001C, 0x90020

    def __init__(self, vol):
        import ctypes
        from ctypes import wintypes
        self.k = k = ctypes.WinDLL('kernel32', use_last_error=True)
        k.CreateFileW.restype = wintypes.HANDLE
        self.h = k.CreateFileW(vol, 0xC0000000, 3, None, 3, 0, None)
        if self.h in (None, wintypes.HANDLE(-1).value):
            sys.exit(f'{vol}: cannot open ({ctypes.get_last_error()})')
        n = wintypes.DWORD()
        for i in range(10):                       # Explorer may hold it for a moment
            if k.DeviceIoControl(self.h, self.LOCK, None, 0, None, 0, ctypes.byref(n), None):
                break
            time.sleep(0.5)
        else:
            k.CloseHandle(self.h)
            sys.exit(f'{vol}: medium is in use (close Explorer windows and files on it)')
        k.DeviceIoControl(self.h, self.DISMOUNT, None, 0, None, 0, ctypes.byref(n), None)
        self.ctypes, self.n = ctypes, n

    def sector(self, lba, data=None):
        """read (data None) or write one 512-byte sector of the volume"""
        c = self.ctypes
        buf = c.create_string_buffer(data if data is not None else 512, 512)
        pos = c.c_longlong()
        self.k.SetFilePointerEx(self.h, c.c_longlong(lba * 512), c.byref(pos), 0)
        f = self.k.WriteFile if data is not None else self.k.ReadFile
        if not f(self.h, buf, 512, c.byref(self.n), None) or self.n.value != 512:
            sys.exit(f'volume sector {lba}: I/O error {c.get_last_error()}')
        return buf.raw

    def close(self):
        if self.h:
            self.k.DeviceIoControl(self.h, self.UNLOCK, None, 0, None, 0, self.ctypes.byref(self.n), None)
            self.k.CloseHandle(self.h)
            self.h = None


def prepare_medium(m):
    """check access; returns an object with close() to release it afterwards"""
    if platform.system() == 'Windows':
        win_admin_or_relaunch()
        return WinVolumeLock(m.vol)
    if m.mount:
        sys.exit(f'{m.vol} is mounted at {m.mount}; unmount it first (udisksctl unmount -b {m.vol})')
    if not os.access(m.dev, os.R_OK | os.W_OK):
        sys.exit(f'{m.dev}: no read/write permission (run with sudo or join group "disk")')
    return None


def new_id(key):
    """give a medium a new random FAT volume serial (cloned sticks share one)"""
    ms = media()
    if platform.system() == 'Windows' and re.fullmatch(r'[A-Za-z]:?', key):
        hit = [m for m in ms if (m.letter or '').upper() == key[0].upper()]
        if not hit:
            sys.exit(f'{key}: no removable medium up to 32 GB with this drive letter')
        m = hit[0]
    elif key.startswith('/dev/'):
        hit = [m for m in ms if key in (m.dev, m.vol)]
        if not hit:
            sys.exit(f'{key}: no removable medium up to 32 GB')
        m = hit[0]
    else:
        m = choose(key, ms)
    if m.why:
        sys.exit(f'{m.dev}: not usable: {m.why}')
    new = int.from_bytes(os.urandom(4), 'little')
    news = f'{new >> 16:04X}-{new & 0xFFFF:04X}'
    ans = input(f'{m.dev} "{m.label}" ({m.size / 1e9:.1f} GB): serial {m.serial} -> {news}? (y/n) ')
    if ans.strip().lower() not in ('y', 'j', 'yes', 'ja'):
        sys.exit('nothing changed')
    lock = prepare_medium(m)
    try:
        if lock:
            rd = lambda lba: lock.sector(lba)
            wr = lambda lba, d: lock.sector(lba, d)
        else:
            f = open(m.vol, 'r+b', buffering=0)

            def rd(lba):
                f.seek(lba * 512)
                return f.read(512)

            def wr(lba, d):
                f.seek(lba * 512)
                f.write(d)
        bs = bytearray(rd(0))
        if bs[510:512] != b'\x55\xaa':
            sys.exit('no FAT boot sector found, nothing changed')
        if bs[0x52:0x5a] == b'FAT32   ':
            off, backup = 0x43, int.from_bytes(bs[0x32:0x34], 'little')
        elif bs[0x36:0x39] == b'FAT':
            off, backup = 0x27, 0
        else:
            sys.exit('unknown FAT variant, nothing changed')
        bs[off:off + 4] = new.to_bytes(4, 'little')
        wr(0, bytes(bs))
        if backup:                                 # FAT32 keeps a copy of the boot sector
            bk = bytearray(rd(backup))
            if bk[510:512] == b'\x55\xaa':
                bk[off:off + 4] = new.to_bytes(4, 'little')
                wr(backup, bytes(bk))
        if not lock:
            f.close()
    finally:
        if lock:
            lock.close()
    print(f'{m.dev}: new volume serial {news}')


def main():
    # SIGTERM (timeout, kill) als normales Ende behandeln, damit finally das
    # Laufwerk mit beendet und kein Prozess verwaist
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(1))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group()
    g.add_argument('--avr', metavar='ELF', nargs='?', const='',
                   help='petSD+ firmware (NODISKEMU .elf for ATmega1284P; default: firmware/NODISKEMU-petSD+.elf if present)')
    g.add_argument('--pico', metavar='BIN', nargs='?', const='',
                   help='petSD2 firmware built with -DPSD2_EMU=1 (default: firmware/petsd2-dip40-emu.bin)')
    ap.add_argument('--sd', metavar='IMG', help='SD card image (MBR + FAT), or a USB stick / SD card chosen by '
                    '5 to 9 characters of its volume name or serial (see --list-drives)')
    ap.add_argument('--list-drives', action='store_true', help='list USB sticks / SD cards usable with --sd')
    ap.add_argument('--new-id', metavar='MEDIUM', help='give a stick a new random volume serial '
                    '(drive letter, /dev/..., or name/serial); asks first')
    ap.add_argument('--cwd', help=argparse.SUPPRESS)
    ap.add_argument('--pause', action='store_true', help=argparse.SUPPRESS)
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
    if a.cwd:                                     # restarted with administrator rights
        os.chdir(a.cwd)
    try:
        run(ap, a)
    finally:
        if a.pause:                               # own console window: keep it readable
            input('press Enter to close this window')


def run(ap, a):
    if a.list_drives:
        list_drives()
        return
    if a.new_id:
        new_id(a.new_id)
        return
    if a.new_sd:
        new_sd(a.new_sd)
        return
    if a.avr is None and a.pico is None:
        ap.error('choose --avr ELF or --pico [BIN]')
    if not a.sd:
        ap.error('--sd IMG is required (create one with --new-sd IMG)')
    lock = None
    if os.path.exists(a.sd):
        card = a.sd
        eep = a.eeprom or os.path.splitext(a.sd)[0] + '.eep'
    else:
        if not 5 <= len(a.sd) <= 9:
            sys.exit(f'{a.sd}: no such image file (a stick is chosen by 5 to 9 characters, see --list-drives)')
        m = choose(a.sd)
        print(f'card  : {m.dev}  "{m.label}"  {m.serial}  {m.size / 1e9:.1f} GB  {m.name}')
        lock = prepare_medium(m)
        card = m.dev
        eep = a.eeprom or os.path.join(HERE, f'usb-{m.serial or "noserial"}.eep')
    try:
        start(a, card, eep)
    finally:
        if lock:
            lock.close()


def start(a, card, eep):

    # VICE-Windows-Build: Ordner vice\ oder so, wie er aus dem Zip kommt (GTK3VICE-...)
    vice_dirs = [os.path.join(HERE, 'vice')] + sorted(glob.glob(os.path.join(HERE, 'GTK3VICE*')), reverse=True)
    xpet = a.xpet or find('xpet', 'XPET', [os.path.join(HERE, 'vice-lre-simlink', 'vice', 'src', 'xpet')] +
                          [os.path.join(d, 'bin', 'xpet') for d in vice_dirs])
    if not xpet:
        sys.exit('xpet not found (build VICE-LRE-SIMLINK, then use --xpet or XPET=...)')

    if a.avr is not None:
        emu = a.petsdsim or find('petsdsim', 'PETSDSIM', [os.path.join(HERE, 'petsdsim', 'petsdsim'),
                                                          os.path.join(HERE, 'petsdsim')])
        if not emu:
            sys.exit('petsdsim not found (cd sim/petsdsim && make; Windows: see README)')
        cmd = [emu, '-w', str(a.port), '-e', eep]
        if a.vcd:
            os.makedirs(a.vcd, exist_ok=True)
            cmd += ['-g', a.vcd]
        elf = a.avr or os.path.join(HERE, 'firmware', 'NODISKEMU-petSD+.elf')
        if not os.path.isfile(elf):
            sys.exit(f'{elf} not found (build NODISKEMU: make CONFIG=configs/config-petSD+, then --avr obj-m1284p-petSD+/NODISKEMU.elf)')
        cmd += [elf, card, a.bus]
    else:
        fw = a.pico or os.path.join(HERE, 'firmware', 'petsd2-dip40-emu.bin')
        emu = a.psd2sim or find('psd2sim', 'PSD2SIM', [os.path.join(HERE, 'psd2sim', 'target', 'release', 'psd2sim'),
                                                        os.path.join(HERE, 'psd2sim')])
        if not emu:
            sys.exit('psd2sim not found (cd sim/psd2sim && cargo build --release)')
        os.environ['PSD_EEPROM'] = '24c32'          # EEPROM on the adapter pad
        os.environ['PSD_EEPROM_FILE'] = eep
        if a.vcd:
            print('note : --vcd is only available for the petSD+ (--avr) so far')
        cmd = [emu, '-b', 'dip40', '-w', str(a.port), '-s', card, fw, a.bus]

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
