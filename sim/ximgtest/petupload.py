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

# Legacy-Modus (U1/U2): BUF2 = 128 Bytes ab $033A (die schnellen Routinen
# werden dort im Legacy-Modus nicht gebraucht), Parameter ab $03E0.
LBUF, LLF, LCNT, LOFF, LIDX = 0x033A, 0x03E0, 0x03E1, 0x03E2, 0x03E3
CHKIN, CHRIN = 0xFFC6, 0xFFCF

def legacy():
    """$027A: JMP RD / JMP WR. RD: Bytes OFF..CNT-1 von Datei LF in den
    Puffer (LF = 0: Nullen). WR: Bytes OFF..CNT-1 aus dem Puffer in Datei LF."""
    A = Asm(0x027A)
    A.j(0x4C, 'rd'); A.j(0x4C, 'wr')
    A.L('rd'); A.a(0xAE, LLF); A.r(0xF0, 'zero'); A.a(0x20, CHKIN)
    A.a(0xAD, LOFF); A.a(0x8D, LIDX)
    A.L('rl'); A.a(0x20, CHRIN); A.a(0xAC, LIDX); A.a(0x99, LBUF)
    A.a(0xEE, LIDX); A.a(0xAD, LIDX); A.a(0xCD, LCNT); A.r(0xD0, 'rl')
    A.L('done'); A.a(0x4C, CLRCHN)
    A.L('zero'); A.a(0xAC, LOFF); A.b(0xA9, 0)
    A.L('zl'); A.a(0x99, LBUF); A.b(0xC8); A.a(0xCC, LCNT); A.r(0xD0, 'zl'); A.b(0x60)
    A.L('wr'); A.a(0xAE, LLF); A.a(0x20, CHKOUT); A.a(0xAD, LOFF); A.a(0x8D, LIDX)
    A.L('wl'); A.a(0xAC, LIDX); A.a(0xB9, LBUF); A.a(0x20, CHROUT)
    A.a(0xEE, LIDX); A.a(0xAD, LIDX); A.a(0xCD, LCNT); A.r(0xD0, 'wl')
    A.j(0x4C, 'done')
    c = A.done()
    assert 0x027A + len(c) <= 0x033A
    return c

# Geraet-zu-Geraet in einem ATN-Zyklus: zuerst das fremde Geraet (P1/D1/S1),
# zuletzt die petSD (P2/D2/S2); P = $40 TALK oder $20 LISTEN, S mit $60.
# N Bytes (0 = 256); der PET quittiert als zusaetzlicher Hoerer mit und zaehlt.
DP1, DD1, DS1, DP2, DD2, DS2, DN, DCNT = 0x03E4, 0x03E5, 0x03E6, 0x03E7, 0x03E8, 0x03E9, 0x03EA, 0x03EB
LIST1, TKSA, UNTLK, UNLSN, ISOUR = 0xF0D7, 0xF193, 0xF1AE, 0xF1B9, 0xF109
FA, C3P0, SATUS, BSOUR, VIA, IEEIS = 0xD4, 0xA0, 0x96, 0xA5, 0xE840, 0xE821
STKEY = 0x9B                              # $EF solange STOP gedrueckt

def d2d(org):
    A = Asm(org)
    A.b(0xA9, 0); A.b(0x85, C3P0); A.b(0x85, SATUS)
    A.a(0xAD, DD1); A.b(0x85, FA); A.a(0xAD, DP1); A.a(0x20, LIST1)       # ATN, P1|D1
    A.a(0xAD, DS1); A.b(0x85, BSOUR); A.a(0x20, ISOUR)                     # S1, ATN bleibt
    A.a(0xAD, DD2); A.b(0x85, FA); A.a(0xAD, DP2); A.a(0x20, LIST1)        # P2|D2
    A.a(0xAD, DS2); A.a(0x20, TKSA)                                        # S2, PET hoert, ATN frei
    A.a(0xAD, DN); A.a(0x8D, DCNT)
    A.L('loop'); A.a(0xAD, VIA); A.b(0x09, 0x02); A.a(0x8D, VIA)      # NRFD frei
    A.L('w1'); A.a(0x2C, VIA); A.r(0x10, 'got')                         # warten DAV low,
    A.b(0xA5, STKEY); A.b(0xC9, 0xEF); A.r(0xD0, 'w1')                  # dabei STOP-Taste
    A.r(0xF0, 'abort')
    A.L('got')
    A.a(0xAD, VIA); A.b(0x29, 0xFD); A.a(0x8D, VIA)                     # NRFD aktiv
    A.b(0xA9, 0x3C); A.a(0x8D, IEEIS)                                   # NDAC frei
    A.L('w2'); A.a(0x2C, VIA); A.r(0x10, 'w2')                          # warten DAV high
    A.b(0xA9, 0x34); A.a(0x8D, IEEIS)                                   # NDAC aktiv
    A.a(0xCE, DCNT); A.r(0xD0, 'loop')
    A.b(0x2C)                                                           # INC ueberspringen
    A.L('abort'); A.b(0xE6, SATUS)                                      # Abbruch: ST <> 0
    A.a(0x20, UNTLK); A.a(0x4C, UNLSN)                                  # UNTLK zieht ATN zuerst
    return A.done()

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
    # Legacy: U1/U2-Routinen ab $027A, Geraet-zu-Geraet ab $033A (der Hex-
    # Lader dort wird im Legacy-Modus nicht mehr gebraucht; der Puffer der
    # Uebertragung ueber den PET liegt auch dort, die beiden schliessen
    # sich aus: dp=1 oder dp=0)
    lg = legacy(); dd = d2d(0x033A)
    assert 0x027A + len(lg) <= 0x033A and 0x033A + len(dd) <= 0x03E0
    for l in lines(697, lg + dd, -(-(len(lg) + len(dd)) // 3)): print(l)
    print(f"legacy {len(lg)} bytes at 634, d2d {len(dd)} bytes at 826", file=sys.stderr)
    print(f"mc {len(m)} loader {len(ld)} drive {len(d)} legacy {len(lg)}", file=sys.stderr)
