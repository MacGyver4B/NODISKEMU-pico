#!/usr/bin/env python3
# Erzeugt die DATA-Zeilen fuer ximg4.bas:
#  - PET-Routine ($033A, zweiter Kassettenpuffer): schickt CNT Bytes aus BUF
#    als "M-W" an die Floppy (Datei 15) und zaehlt die Adresse weiter.
#  - Laufwerkscode (drive4040.py) als DATA, Ende -1.
# Aufruf: petupload.py   -> Zeilen auf stdout (MC ab 23, Laufwerkscode ab 2000)
import sys
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from drive4040 import Asm, build as drive_build
CHKOUT, CLRCHN, CHROUT = 0xFFC9, 0xFFCC, 0xFFD2
BUF, CNT, IDX, ADLO, ADHI, FOUND = 0x03D0, 0x03F0, 0x03F1, 0x03F2, 0x03F3, 0x03F4
ORG, PAR = 0x2000, 0x2343

def mc():
    A = Asm(0x033A)
    A.b(0xA2, 15); A.a(0x20, CHKOUT)
    for c in (0x4D, 0x2D, 0x57):                               # "M-W"
        A.b(0xA9, c); A.a(0x20, CHROUT)
    for v in (ADLO, ADHI, CNT):
        A.a(0xAD, v); A.a(0x20, CHROUT)
    A.b(0xA9, 0); A.a(0x8D, IDX)
    A.L('wr'); A.a(0xAC, IDX); A.a(0xB9, BUF); A.a(0x20, CHROUT)
    A.a(0xEE, IDX); A.a(0xAD, IDX); A.a(0xCD, CNT); A.r(0xD0, 'wr')
    A.a(0x20, CLRCHN)
    A.b(0x18); A.a(0xAD, ADLO); A.a(0x6D, CNT); A.a(0x8D, ADLO); A.r(0x90, 'nc'); A.a(0xEE, ADHI)
    A.L('nc'); A.b(0x60)
    c = A.done()
    assert 0x033A + len(c) <= BUF
    return c

def loader(writer):
    """$0282 (Kassettenpuffer 1, hinter der EOI-Schleife bei $027A): sucht ab
    TXTTAB ($28/$29) die Zeilen 'DATA#<hex>', wandelt die Hex-Ziffern um und
    ruft je 32 Bytes den Schreiber auf. Selbstmodifizierend, ohne Zeropage."""
    A = Asm(0x0282)
    A.b(0xA9, 0); A.a(0x8D, CNT); A.a(0x8D, FOUND)
    A.b(0xA5, 0x28); A.j(0x8D, 'p1'); A.b(0xA5, 0x29); A.j(0x8D, 'p1h')
    A.L('line')
    A.b(0xA0, 1); A.j(0x20, 'get'); A.r(0xF0, 'end')           # Link hi = 0: Ende
    A.b(0xA0, 4); A.j(0x20, 'get'); A.b(0xC9, 0x83); A.r(0xD0, 'skip')   # DATA?
    A.b(0xC8); A.j(0x20, 'get'); A.b(0xC9, 0x23); A.r(0xD0, 'skip')      # '#'?
    A.a(0xEE, FOUND)
    A.L('hex'); A.b(0xC8); A.j(0x20, 'get'); A.r(0xF0, 'next')
    A.j(0x20, 'nib'); A.b(0x0A, 0x0A, 0x0A, 0x0A); A.a(0x8D, IDX)
    A.b(0xC8); A.j(0x20, 'get'); A.j(0x20, 'nib'); A.a(0x0D, IDX)
    A.a(0xAE, CNT); A.a(0x9D, BUF); A.b(0xE8); A.a(0x8E, CNT)
    A.b(0xE0, 32); A.r(0xD0, 'hex')
    A.b(0x98, 0x48)                                            # Y retten
    A.a(0x20, writer); A.b(0xA9, 0); A.a(0x8D, CNT)
    A.b(0x68, 0xA8); A.j(0x4C, 'hex')
    A.L('skip'); A.a(0xAD, FOUND); A.r(0xD0, 'end')            # Codezeilen vorbei
    A.L('next')                                                # naechste Zeile
    A.b(0xA0, 0); A.j(0x20, 'get'); A.b(0x48)
    A.b(0xC8); A.j(0x20, 'get'); A.j(0x8D, 'p1h'); A.b(0x68); A.j(0x8D, 'p1')
    A.j(0x4C, 'line')
    A.L('end'); A.a(0xAD, CNT); A.r(0xF0, 'ret'); A.a(0x20, writer)
    A.L('ret'); A.b(0x60)
    A.L('get'); A.b(0xB9); A.L('p1'); A.b(0); A.L('p1h'); A.b(0); A.b(0x60)   # LDA $xxxx,Y
    A.L('nib'); A.b(0xC9, 0x41); A.r(0x90, 'dig'); A.b(0xE9, 0x37, 0x60)       # A-F (C=1)
    A.L('dig'); A.b(0x29, 0x0F, 0x60)
    c = A.done()
    assert 0x0282 + len(c) <= 0x033A, len(c)
    return c

def lines(first, data, per, end=None):
    out = []; n = first
    for i in range(0, len(data), per):
        out.append(f"{n} data" + ",".join(map(str, data[i:i + per]))); n += 1
    if end is not None:
        out[-1] += f",{end}"
    return out

if __name__ == '__main__':
    m = mc(); d, _ = drive_build(ORG, PAR); ld = loader(0x033A)
    for l in lines(23, m + ld, 21): print(l)
    n = 2000
    for i in range(0, len(d), 32):
        print(f"{n} data#" + bytes(d[i:i + 32]).hex()); n += 1
    print(f"mc {len(m)} loader {len(ld)} drive {len(d)}", file=sys.stderr)
