#!/usr/bin/env python3
# PET-Routine fuer ximg4 ($033A, zweiter Kassettenpuffer): liest ximgdrv.prg
# aus Datei #3 (Ladeadresse schon ueberlesen) in Haeppchen zu 32 Bytes und
# schreibt jedes per "M-W" auf Kanal 15 ab $2000 in die Floppy, bis EOF.
# Ausgabe: BASIC-DATA-Zeilen ab Zeilennummer argv[1] (Vorgabe 23).
import sys
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from drive4040 import Asm
CHKIN, CHKOUT, CLRCHN, CHRIN, CHROUT = 0xFFC6, 0xFFC9, 0xFFCC, 0xFFCF, 0xFFD2
BUF, CNT, IDX, ADLO, ADHI, STS = 0x03D0, 0x03F0, 0x03F1, 0x03F2, 0x03F3, 0x03F4

def build():
    A = Asm(0x033A)
    A.b(0xA9, 0x00); A.a(0x8D, ADLO); A.b(0xA9, 0x20); A.a(0x8D, ADHI)
    A.L('loop')
    A.b(0xA9, 0); A.b(0x85, 0x96); A.a(0x8D, CNT)              # ST = 0, CNT = 0
    A.b(0xA2, 3); A.a(0x20, CHKIN)
    A.L('rd'); A.a(0x20, CHRIN); A.a(0xAC, CNT); A.a(0x99, BUF); A.b(0xC8); A.a(0x8C, CNT)
    A.b(0xA5, 0x96); A.a(0x8D, STS); A.r(0xD0, 'rde'); A.b(0xC0, 32); A.r(0xD0, 'rd')
    A.L('rde'); A.a(0x20, CLRCHN)
    A.a(0xAD, CNT); A.r(0xF0, 'fin')
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
    A.L('nc'); A.a(0xAD, STS); A.r(0xD0, 'fin'); A.j(0x4C, 'loop')
    A.L('fin'); A.b(0x60)
    c = A.done()
    assert 0x033A + len(c) <= BUF
    return c

if __name__ == '__main__':
    c = build(); n = int(sys.argv[1]) if len(sys.argv) > 1 else 23
    for i in range(0, len(c), 21):
        print(f"{n} data" + ",".join(map(str, c[i:i + 21]))); n += 1
    print(len(c), file=sys.stderr)
