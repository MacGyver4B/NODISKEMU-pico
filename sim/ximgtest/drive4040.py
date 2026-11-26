#!/usr/bin/env python3
# Laufwerkscode fuer den DOS-Prozessor der 4040 (per M-E, Interrupts aus).
# Der Laufwerks-Prozessor arbeitet unveraendert ueber die Job-Queue.
#
# Einstiege:
#   READ  (ORG+0): Spur lesen. Je Sektor 258 Bytes an den Controller:
#                  Sektornummer, Job-Ergebnis (1 = ok), 256 Datenbytes.
#                  Reihenfolge = Fertigstellung.
#   WRITE (ORG+3): Spur schreiben und pruefen. Vom Controller je Sektor
#                  (in der Reihenfolge der Sektorliste P+$37) 256 Datenbytes + 2 Bytes Pruefsumme.
#                  Danach Pruefdurchgang: Lese-Jobs, Pruefsumme auf dem
#                  DOS-Prozessor, Vergleich. Zurueck: N Ergebnisbytes
#                  (auf gerade Anzahl aufgefuellt): 1 = ok, 7 = Pruefsumme
#                  falsch, sonst Job-Ergebnis.
#
# Handshake, je Byte:
#   Floppy -> Controller: Floppy legt Byte roh auf DIO und schaltet DAV um,
#                         Controller quittiert durch Umschalten von NRFD.
#   Controller -> Floppy: Controller legt das invertierte Byte an und
#                         schaltet DAV um, Floppy quittiert mit NRFD.
#   Immer gerade Bytezahlen: danach sind DAV und NRFD wieder frei.
#
#   AREAD (ORG+6), AWRITE (ORG+9): ganze Diskette in einem Durchgang, Spur
#                  FIRST bis LAST-1 (LAST = letzte Spur + 1), je Spur wie READ/WRITE. Sektoren je Spur
#                  aus der Zonentabelle ZT (bis 8 Paare: letzte Spur der
#                  Zone + 1, Sektorzahl). Die Sektorliste P+$37 muss
#                  0,1,2,..,28 enthalten (schreibt der Controller).
#
# Pruefsumme (Controller rechnet genauso): a = 0, c = 0, s2 = 0
#   je Byte b: a = a + b + c (c = Uebertrag), s1 = a; a = a + s2 + c, s2 = a; a = s1
#   Ergebnis s1, s2 (Uebertrag laeuft durch, kein CLC im Durchlauf)
import sys

IEEEDI, IEEEDO, PAD2, PBD2 = 0x0200, 0x0202, 0x0280, 0x0282
ATNA, DACO, RFDO, EOIO, DAVO = 0x01, 0x02, 0x04, 0x08, 0x10
JOBS, HDRS = 0x1003, 0x1021
PLO, PHI, RLO, S1, S2 = 0x02, 0x03, 0x04, 0x05, 0x06   # DOS-TEMP

class Asm:
    def __init__(self, org):
        self.org, self.c, self.lab, self.fix = org, [], {}, []
    def pc(self): return self.org + len(self.c)
    def L(self, n):
        assert n not in self.lab, n
        self.lab[n] = self.pc()
    def b(self, *x): self.c.extend(x)
    def a(self, op, v): self.b(op, v & 255, (v >> 8) & 255)
    def j(self, op, name):
        self.b(op); self.fix.append((len(self.c), name, 'abs')); self.b(0, 0)
    def r(self, op, name):
        self.b(op); self.fix.append((len(self.c), name, 'rel')); self.b(0)
    def patchable(self, op, name):       # abs,Y mit Low 00, High wird gepatcht
        self.b(op, 0x00); self.L(name); self.b(0x00)
    def done(self):
        for pos, n, k in self.fix:
            t = self.lab[n]
            if k == 'rel':
                d = t - (self.org + pos + 1)
                assert -128 <= d < 128, (n, d)
                self.c[pos] = d & 255
            else:
                self.c[pos] = t & 255
                self.c[pos + 1] = t >> 8
        return self.c

def build(org=0x2000, par=0x2340):
    P = par
    T, N, DRV, R = P, P + 1, P + 2, P + 3
    JOBN, BUFH, IDJ = P + 4, P + 14, P + 24
    SEC, TRY = P + 25, P + 35
    NEXT, SENT, SLOT, RES, ID1, ID2, CMDJ, WSLOT, CNT, RTMP = (P + 45, P + 46, P + 47, P + 48,
                                                         P + 49, P + 50, P + 51, P + 52, P + 53, P + 54)
    ORDER = P + 0x37                      # Sektorreihenfolge (29)
    SUM, RESULT = P + 0x54, P + 0x8E      # 2*29, 29 -> bis P+0xAA
    FIRST, LAST, ZT = P + 0xAB, P + 0xAC, P + 0xAD   # ZT: 16 Bytes -> bis P+0xBC
    A = Asm(org)
    A.j(0x4C, 'rentry')
    A.j(0x4C, 'wentry')
    A.b(0xA2, 0, 0x2C)                    # AREAD: X = 0, BIT abs ueberspringt
    A.b(0xA2, 3)                          # AWRITE: X = 3 (Abstand rentry -> wentry im JMP-Tisch)

    # ---------------- ganze Diskette (Sektorliste P+$37 = 0,1,2,.. vom Controller)
    A.a(0xBD, org + 1); A.j(0x8D, 'acall'); A.a(0xBD, org + 2); A.j(0x8D, 'acall2')
    A.a(0xAD, FIRST); A.a(0x8D, T)
    A.L('at'); A.b(0xA2, 0); A.a(0xAD, T)
    A.L('az'); A.a(0xDD, ZT); A.r(0x90, 'afound')    # ZT: letzte Spur der Zone + 1
    A.b(0xE8, 0xE8); A.r(0xD0, 'az')
    A.L('afound'); A.a(0xBD, ZT + 1); A.a(0x8D, N)
    A.b(0x20); A.L('acall'); A.b(0); A.L('acall2'); A.b(0)
    A.a(0xEE, T); A.a(0xAD, T); A.a(0xCD, LAST); A.r(0xD0, 'at')   # LAST: letzte Spur + 1
    A.b(0x60)

    # ---------------- gemeinsame Unterprogramme
    A.L('init')
    A.a(0xAD, PAD2); A.b(0x09, DAVO | EOIO | RFDO, 0x29, 0xFF ^ DACO)
    A.a(0x8D, PAD2); A.b(0x85, PHI)
    A.b(0x29, 0xFF ^ DAVO, 0x85, PLO)
    A.b(0xA5, PHI, 0x29, 0xFF ^ RFDO, 0x85, RLO)
    A.a(0xAD, IDJ); A.b(0x0A, 0x0A, 0x0A, 0xAA)
    A.a(0xBD, HDRS); A.a(0x8D, ID1); A.a(0xBD, HDRS + 1); A.a(0x8D, ID2)
    A.b(0xA9, 0); A.a(0x8D, NEXT); A.a(0x8D, SENT); A.a(0x8D, WSLOT)
    A.a(0xAE, R); A.b(0xCA, 0xA9, 0xFF)
    A.L('in1'); A.a(0x9D, SEC); A.b(0xCA); A.r(0x10, 'in1')
    A.b(0x60)

    # queue: Slot X mit naechstem Sektor (falls vorhanden), dann setjob
    A.L('queue')
    A.a(0xAC, NEXT); A.a(0xCC, N); A.r(0xB0, 'qret')            # LDY NEXT / CPY N
    A.a(0xB9, ORDER); A.a(0x9D, SEC); A.a(0xEE, NEXT); A.b(0xA9, 2); A.a(0x9D, TRY)
    A.L('setjob')
    A.a(0xBD, JOBN); A.b(0x0A, 0x0A, 0x0A, 0xA8)
    A.a(0xAD, ID1); A.a(0x99, HDRS); A.a(0xAD, ID2); A.a(0x99, HDRS + 1)
    A.a(0xAD, T); A.a(0x99, HDRS + 2); A.a(0xBD, SEC); A.a(0x99, HDRS + 3)
    A.a(0xBC, JOBN); A.a(0xAD, DRV); A.a(0x0D, CMDJ); A.a(0x99, JOBS)
    A.L('qret'); A.b(0x60)

    # patch: High-Byte des Puffers von Slot X in alle Pufferzugriffe
    A.L('patch')
    A.a(0xBD, BUFH)
    for n in ('p0', 'p1', 'p2', 'q1', 'q2', 'c1'):
        A.j(0x8D, n)
    A.b(0x60)

    # setres: A = Ergebnis, X = Slot. Erstes Problem je Sektor bleibt stehen.
    A.L('setres')
    A.b(0xC9, 1); A.r(0xF0, 'sret')
    A.a(0x8D, RTMP); A.a(0xBC, SEC); A.a(0xB9, RESULT); A.b(0xC9, 1)
    A.r(0xD0, 'sret'); A.a(0xAD, RTMP); A.a(0x99, RESULT)
    A.L('sret'); A.b(0x60)

    # sendblk: Slot X: Sektor, RES, 256 Datenbytes
    A.L('sendblk')
    A.j(0x20, 'patch')
    A.a(0xBD, SEC); A.b(0xAA)
    A.a(0x8E, IEEEDO); A.b(0xA5, PLO); A.a(0x8D, PAD2)
    A.a(0xAE, RES)
    A.L('h1'); A.a(0x2C, PBD2); A.r(0x30, 'h1')
    A.a(0x8E, IEEEDO); A.b(0xA5, PHI); A.a(0x8D, PAD2)
    A.L('h2'); A.a(0x2C, PBD2); A.r(0x10, 'h2')
    A.b(0xA0, 0); A.patchable(0xBE, 'p0')
    A.L('sl')
    A.a(0x8E, IEEEDO); A.b(0xA5, PLO); A.a(0x8D, PAD2)
    A.b(0xC8); A.patchable(0xBE, 'p1')
    A.L('sw1'); A.a(0x2C, PBD2); A.r(0x30, 'sw1')
    A.a(0x8E, IEEEDO); A.b(0xA5, PHI); A.a(0x8D, PAD2)
    A.b(0xC8); A.patchable(0xBE, 'p2')
    A.L('sw2'); A.a(0x2C, PBD2); A.r(0x10, 'sw2')
    A.b(0xC0, 0); A.r(0xD0, 'sl')
    A.b(0x60)

    # rcvblk: 256 Bytes in den gepatchten Puffer, 2 Bytes nach SUM+2*NEXT
    A.L('rcvblk')
    A.b(0xA0, 0)
    A.L('rl1'); A.a(0x2C, PAD2); A.r(0x70, 'rl1')                 # BVS: DAV noch high
    A.a(0xAD, IEEEDI); A.patchable(0x99, 'q1'); A.b(0xA5, RLO); A.a(0x8D, PAD2); A.b(0xC8)
    A.L('rl2'); A.a(0x2C, PAD2); A.r(0x50, 'rl2')                 # BVC: DAV noch low
    A.a(0xAD, IEEEDI); A.patchable(0x99, 'q2'); A.b(0xA5, PHI); A.a(0x8D, PAD2); A.b(0xC8)
    A.r(0xD0, 'rl1')
    A.a(0xAC, NEXT); A.a(0xB9, ORDER); A.b(0x0A, 0xA8)
    A.L('rs1'); A.a(0x2C, PAD2); A.r(0x70, 'rs1')
    A.a(0xAD, IEEEDI); A.a(0x99, SUM); A.b(0xA5, RLO); A.a(0x8D, PAD2)
    A.L('rs2'); A.a(0x2C, PAD2); A.r(0x50, 'rs2')
    A.a(0xAD, IEEEDI); A.a(0x99, SUM + 1); A.b(0xA5, PHI); A.a(0x8D, PAD2)
    A.b(0x60)

    # csum: Pruefsumme des Puffers von Slot SLOT, Vergleich mit SUM[2*SEC]
    A.L('csum')
    A.b(0xA9, 0, 0x85, S2, 0x18, 0xA0, 0)
    A.L('cl'); A.patchable(0x79, 'c1')                            # ADC buf,Y
    A.b(0x85, S1, 0x65, S2, 0x85, S2, 0xA5, S1, 0xC8); A.r(0xD0, 'cl')
    A.a(0xAE, SLOT); A.a(0xBD, SEC); A.b(0x0A, 0xA8)
    A.b(0xA5, S1); A.a(0xD9, SUM); A.r(0xD0, 'cbad')
    A.b(0xA5, S2); A.a(0xD9, SUM + 1); A.r(0xD0, 'cbad')
    A.b(0x60)
    A.L('cbad'); A.b(0xA9, 7); A.j(0x4C, 'setres')

    # sendres: RESULT, CNT Bytes (gerade)
    A.L('sendres')
    A.b(0xA0, 0); A.a(0xBE, RESULT)
    A.L('tl')
    A.a(0x8E, IEEEDO); A.b(0xA5, PLO); A.a(0x8D, PAD2); A.b(0xC8); A.a(0xBE, RESULT)
    A.L('tw1'); A.a(0x2C, PBD2); A.r(0x30, 'tw1')
    A.a(0x8E, IEEEDO); A.b(0xA5, PHI); A.a(0x8D, PAD2); A.b(0xC8); A.a(0xBE, RESULT)
    A.L('tw2'); A.a(0x2C, PBD2); A.r(0x10, 'tw2')
    A.a(0xCC, CNT); A.r(0xD0, 'tl')
    A.b(0x60)

    # ---------------- READ
    A.L('rentry')
    A.j(0x20, 'init'); A.b(0xA9, 0x80); A.a(0x8D, CMDJ)
    A.b(0xA2, 0)
    A.L('ri'); A.j(0x20, 'queue'); A.b(0xE8); A.a(0xEC, R); A.r(0xD0, 'ri')
    A.L('rmain'); A.b(0xA2, 0)
    A.L('rscan')
    A.a(0xBD, SEC); A.r(0x30, 'rnext')
    A.a(0xBC, JOBN); A.a(0xB9, JOBS); A.r(0x30, 'rnext')
    A.b(0xC9, 2); A.r(0x90, 'rgood')
    A.a(0xBC, TRY); A.r(0xF0, 'rgood')
    A.a(0xDE, TRY); A.j(0x20, 'setjob'); A.j(0x4C, 'rnext')
    A.L('rgood')
    A.a(0x8D, RES); A.a(0x8E, SLOT); A.j(0x20, 'sendblk')
    A.a(0xEE, SENT); A.a(0xAE, SLOT); A.b(0xA9, 0xFF); A.a(0x9D, SEC); A.j(0x20, 'queue')
    A.L('rnext')
    A.a(0xAD, SENT); A.a(0xCD, N); A.r(0xF0, 'fin')
    A.b(0xE8); A.a(0xEC, R); A.r(0xD0, 'rscan'); A.j(0x4C, 'rmain')
    A.L('fin')
    A.b(0xA9, 0xFF); A.a(0x8D, IEEEDO); A.b(0x60)

    # ---------------- WRITE + Pruefen
    A.L('wentry')
    A.j(0x20, 'init'); A.b(0xA9, 0x90); A.a(0x8D, CMDJ)
    A.a(0xAC, N); A.b(0x88, 0xA9, 1)
    A.L('wr0'); A.a(0x99, RESULT); A.b(0x88); A.r(0x10, 'wr0')
    A.L('wloop')
    A.a(0xAD, NEXT); A.a(0xCD, N); A.r(0xF0, 'wdrain')
    A.a(0xAE, WSLOT)
    A.a(0xBD, SEC); A.r(0x30, 'wfree')
    A.L('wwait'); A.a(0xBC, JOBN); A.a(0xB9, JOBS); A.r(0x30, 'wwait')
    A.j(0x20, 'setres'); A.b(0xA9, 0xFF); A.a(0x9D, SEC)
    A.L('wfree')
    A.j(0x20, 'patch'); A.j(0x20, 'rcvblk')
    A.a(0xAE, WSLOT)
    A.a(0xAC, NEXT); A.a(0xB9, ORDER); A.a(0x9D, SEC); A.a(0xEE, NEXT); A.j(0x20, 'setjob')
    A.b(0xE8); A.a(0xEC, R); A.r(0xD0, 'ws1'); A.b(0xA2, 0)
    A.L('ws1'); A.a(0x8E, WSLOT); A.j(0x4C, 'wloop')
    A.L('wdrain'); A.b(0xA2, 0)
    A.L('wd1'); A.a(0xBD, SEC); A.r(0x30, 'wd3')
    A.L('wd2'); A.a(0xBC, JOBN); A.a(0xB9, JOBS); A.r(0x30, 'wd2')
    A.j(0x20, 'setres'); A.b(0xA9, 0xFF); A.a(0x9D, SEC)
    A.L('wd3'); A.b(0xE8); A.a(0xEC, R); A.r(0xD0, 'wd1')
    # Pruefdurchgang
    A.b(0xA9, 0x80); A.a(0x8D, CMDJ); A.b(0xA9, 0); A.a(0x8D, NEXT); A.a(0x8D, SENT)
    A.b(0xA2, 0)
    A.L('vi'); A.j(0x20, 'queue'); A.b(0xE8); A.a(0xEC, R); A.r(0xD0, 'vi')
    A.L('vmain'); A.b(0xA2, 0)
    A.L('vscan')
    A.a(0xBD, SEC); A.r(0x30, 'vnext')
    A.a(0xBC, JOBN); A.a(0xB9, JOBS); A.r(0x30, 'vnext')
    A.b(0xC9, 2); A.r(0x90, 'vgood')
    A.a(0xBC, TRY); A.r(0xF0, 'vbad')
    A.a(0xDE, TRY); A.j(0x20, 'setjob'); A.j(0x4C, 'vnext')
    A.L('vbad'); A.j(0x20, 'setres'); A.j(0x4C, 'vdone')
    A.L('vgood')
    A.a(0x8E, SLOT); A.j(0x20, 'patch'); A.j(0x20, 'csum'); A.a(0xAE, SLOT)
    A.L('vdone')
    A.a(0xEE, SENT); A.b(0xA9, 0xFF); A.a(0x9D, SEC); A.j(0x20, 'queue')
    A.L('vnext')
    A.a(0xAD, SENT); A.a(0xCD, N); A.r(0xF0, 'vfin')
    A.b(0xE8); A.a(0xEC, R); A.r(0xD0, 'vscan'); A.j(0x4C, 'vmain')
    A.L('vfin')
    A.a(0xAD, N); A.b(0x18, 0x69, 1, 0x29, 0xFE); A.a(0x8D, CNT)
    A.j(0x20, 'sendres')
    A.b(0xA9, 0xFF); A.a(0x8D, IEEEDO); A.b(0x60)

    code = A.done()
    assert org + len(code) <= par, len(code)
    assert par + 0xBD <= 0x2400, hex(par)     # $2400 spiegelt $2000
    return code, A.lab

def fletcher(data):
    a = c = s2 = 0
    for b in data:
        t = a + b + c; a = t & 255; c = t >> 8; s1 = a
        t = a + s2 + c; s2 = t & 255; c = t >> 8; a = s1
    return a, s2

def src_header(code, org, par):
    out = ['/* generated by sim/ximgtest/drive4040.py - do not edit',
           ' *',
           ' * Drive code for the DOS processor of DOS 2.1 (4040) and DOS 2.7',
           ' * (8050/8250/SFD-1001): track read and track write+verify through the',
           ' * job queue, two-wire handshake. See drive4040.py for the protocol.',
           ' */',
           '#define XIMG_DRV_ORG    0x%04x' % org,
           '#define XIMG_DRV_PAR    0x%04x' % par,
           '#define XIMG_DRV_READ   0x%04x' % org,
           '#define XIMG_DRV_WRITE  0x%04x' % (org + 3),
           '#define XIMG_DRV_AREAD  0x%04x' % (org + 6),
           '#define XIMG_DRV_AWRITE 0x%04x' % (org + 9),
           '/* parameter offsets */',
           '#define XIMG_P_T     0',
           '#define XIMG_P_N     1',
           '#define XIMG_P_DRV   2',
           '#define XIMG_P_R     3',
           '#define XIMG_P_JOBN  4',
           '#define XIMG_P_BUFH  14',
           '#define XIMG_P_IDJ   24',
           '#define XIMG_P_ORDER 0x37',
           '#define XIMG_P_FIRST 0xab',
           '#define XIMG_P_END   0xac   /* last track + 1 */',
           '#define XIMG_P_ZT    0xad',
           'static const uint8_t ximg_drv_code[%d] XIMG_PROGMEM = {' % len(code)]
    for i in range(0, len(code), 12):
        out.append('    ' + ', '.join('0x%02x' % v for v in code[i:i + 12]) + ',')
    out.append('};')
    return '\n'.join(out) + '\n'

if __name__ == '__main__':
    ORG, PAR = 0x2000, 0x2343
    code, lab = build(ORG, PAR)
    if len(sys.argv) > 1 and sys.argv[1] == '--src':
        sys.stdout.write(src_header(code, ORG, PAR))
        sys.exit(0)
    print('/* drive4040.py: Spur lesen/schreiben+pruefen, Code bei $%04X (%d Bytes), Parameter bei $%04X */'
          % (ORG, len(code), PAR))
    print('#define D4040_ORG 0x%04x' % ORG)
    print('#define D4040_PAR 0x%04x' % PAR)
    print('#define D4040_READ 0x%04x' % ORG)
    print('#define D4040_WRITE 0x%04x' % (ORG + 3))
    print('#define D4040_ORDER 0x37   /* offset of the sector order table in the parameters */')
    print('static const uint8_t d4040_code[] = {')
    for i in range(0, len(code), 12):
        print('    ' + ', '.join('0x%02x' % v for v in code[i:i + 12]) + ',')
    print('};')
