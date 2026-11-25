#!/usr/bin/env python3
# Spurlesen fuer den DOS-Prozessor der 4040 (laeuft per M-E, Interrupts aus).
#
# Der Laufwerks-Prozessor arbeitet wie immer ueber die Job-Queue. Dieser Code
# stellt Lese-Jobs fuer alle Sektoren einer Spur in einen Ring freier Puffer,
# schickt jeden fertigen Puffer sofort ueber den Bus und belegt ihn mit dem
# naechsten Sektor neu.
#
# Je Sektor 258 Bytes: Sektornummer, Job-Ergebnis (1 = ok), 256 Datenbytes.
# Reihenfolge = Fertigstellung. Daten roh auf DIO (Controller invertiert).
# Handshake: Floppy schaltet DAV je Byte um, Controller quittiert mit NRFD.
#
# Parameter (vom Controller per M-W, in der Codeseite):
#   P+0 Spur, P+1 Sektoren, P+2 Laufwerk, P+3 Ringgroesse,
#   P+4.. Jobnummern (10), P+14.. Puffer-High-Bytes (10), P+24 Job mit ID
import sys

IEEEDO, PAD2, PBD2 = 0x0202, 0x0280, 0x0282
DAVO = 0x10
JOBS, HDRS = 0x1003, 0x1021
PLO, PHI = 0x02, 0x03            # DOS-TEMP, schneller Zugriff

class Asm:
    def __init__(self, org):
        self.org, self.c, self.lab, self.fix = org, [], {}, []
    def L(self, n): self.lab[n] = self.org + len(self.c)
    def b(self, *x): self.c.extend(x)
    def w(self, v): self.c.extend([v & 255, (v >> 8) & 255])
    def abs_(self, op, v): self.b(op); self.w(v)
    def ref(self, op, name):          # absolute Adresse eines Labels
        self.b(op); self.fix.append((len(self.c), name, 'abs')); self.w(0)
    def br(self, op, name):
        self.b(op); self.fix.append((len(self.c), name, 'rel')); self.b(0)
    def done(self):
        for pos, n, k in self.fix:
            a = self.lab[n]
            if k == 'rel':
                d = a - (self.org + pos + 1)
                assert -128 <= d < 128, n
                self.c[pos] = d & 255
            else:
                self.c[pos] = a & 255; self.c[pos + 1] = a >> 8
        return self.c

def assemble(org, par):
    T, N, DRV, R = par, par + 1, par + 2, par + 3
    JOBN, BUFH, IDJ = par + 4, par + 14, par + 24     # je 10 Eintraege
    SEC, TRY = par + 25, par + 35            # je Slot: Sektor ($FF frei), Restversuche
    NEXT, SENT, SLOT, RES, ID1, ID2 = par + 45, par + 46, par + 47, par + 48, par + 49, par + 50
    a = Asm(org)
    # --- Start: DAV-Werte, ID aus dem Header des ID-Jobs
    a.abs_(0xAD, PAD2); a.b(0x09, DAVO); a.abs_(0x8D, PAD2); a.b(0x85, PHI)
    a.b(0x29, 0xFF ^ DAVO); a.b(0x85, PLO)
    a.abs_(0xAD, IDJ); a.b(0x0A, 0x0A, 0x0A, 0xAA)                # ASL x3 / TAX
    a.abs_(0xBD, HDRS); a.abs_(0x8D, ID1)
    a.abs_(0xBD, HDRS + 1); a.abs_(0x8D, ID2)
    a.b(0xA9, 0); a.abs_(0x8D, NEXT); a.abs_(0x8D, SENT)
    a.b(0xA2, 0)                                                  # LDX #0
    a.L('init')
    a.b(0xA9, 0xFF); a.abs_(0x9D, SEC)                            # STA SEC,X
    a.ref(0x20, 'queue')                                          # JSR queue
    a.b(0xE8); a.abs_(0xEC, R); a.br(0xD0, 'init')                # INX / CPX R / BNE
    # --- Hauptschleife: fertige Jobs suchen
    a.L('main'); a.b(0xA2, 0)
    a.L('scan')
    a.abs_(0xBD, SEC); a.br(0x30, 'nexts')                        # LDA SEC,X / BMI
    a.abs_(0xBC, JOBN); a.abs_(0xB9, JOBS); a.br(0x30, 'nexts')   # LDY JOBN,X / LDA JOBS,Y / BMI
    a.b(0xC9, 2); a.br(0x90, 'good')                              # CMP #2 / BCC good
    a.abs_(0xBC, TRY); a.br(0xF0, 'good')                         # LDY TRY,X / BEQ good (keine Versuche mehr)
    a.abs_(0xDE, TRY); a.ref(0x20, 'setjob'); a.ref(0x4C, 'nexts')  # DEC TRY,X / JSR setjob / JMP
    a.L('good')
    a.abs_(0x8D, RES); a.abs_(0x8E, SLOT)                         # STA RES / STX SLOT
    a.ref(0x20, 'send')
    a.abs_(0xEE, SENT); a.abs_(0xAE, SLOT)
    a.b(0xA9, 0xFF); a.abs_(0x9D, SEC)
    a.ref(0x20, 'queue')
    a.L('nexts')
    a.abs_(0xAD, SENT); a.abs_(0xCD, N); a.br(0xF0, 'fin')
    a.b(0xE8); a.abs_(0xEC, R); a.br(0xD0, 'scan'); a.ref(0x4C, 'main')
    a.L('fin')
    a.b(0xA9, 0xFF); a.abs_(0x8D, IEEEDO); a.b(0x60)              # Daten frei / RTS
    # --- queue: Slot X mit naechstem Sektor belegen (falls noch einer da)
    a.L('queue')
    a.abs_(0xAD, NEXT); a.abs_(0xCD, N); a.br(0xB0, 'qret')       # LDA NEXT / CMP N / BCS
    a.abs_(0x9D, SEC); a.abs_(0xEE, NEXT)
    a.b(0xA9, 2); a.abs_(0x9D, TRY)
    # --- setjob: Header + Job fuer Slot X
    a.L('setjob')
    a.abs_(0xBD, JOBN); a.b(0x0A, 0x0A, 0x0A, 0xA8)               # TAY = Job*8
    a.abs_(0xAD, ID1); a.abs_(0x99, HDRS)
    a.abs_(0xAD, ID2); a.abs_(0x99, HDRS + 1)
    a.abs_(0xAD, T); a.abs_(0x99, HDRS + 2)
    a.abs_(0xBD, SEC); a.abs_(0x99, HDRS + 3)
    a.abs_(0xBC, JOBN); a.abs_(0xAD, DRV); a.b(0x09, 0x80); a.abs_(0x99, JOBS)
    a.L('qret'); a.b(0x60)
    # --- send: Sektor, Ergebnis, 256 Bytes aus Puffer BUFH,X
    a.L('send')
    a.abs_(0xBD, BUFH)
    a.ref(0x8D, 'p0'); a.ref(0x8D, 'p1'); a.ref(0x8D, 'p2')       # High-Bytes eintragen
    a.abs_(0xBD, SEC); a.b(0xAA)                                  # X = Sektor
    a.abs_(0x8E, IEEEDO); a.b(0xA5, PLO); a.abs_(0x8D, PAD2)      # Byte 1, DAV low
    a.abs_(0xAE, RES)
    a.L('h1'); a.abs_(0x2C, PBD2); a.br(0x30, 'h1')
    a.abs_(0x8E, IEEEDO); a.b(0xA5, PHI); a.abs_(0x8D, PAD2)      # Byte 2, DAV high
    a.L('h2'); a.abs_(0x2C, PBD2); a.br(0x10, 'h2')
    a.b(0xA0, 0)
    a.b(0xBE, 0x00); a.L('p0'); a.b(0x00)                         # LDX buf,Y  (p0 = High-Byte)
    a.L('loop')
    a.abs_(0x8E, IEEEDO); a.b(0xA5, PLO); a.abs_(0x8D, PAD2)
    a.b(0xC8); a.b(0xBE, 0x00); a.L('p1'); a.b(0x00)
    a.L('w1'); a.abs_(0x2C, PBD2); a.br(0x30, 'w1')
    a.abs_(0x8E, IEEEDO); a.b(0xA5, PHI); a.abs_(0x8D, PAD2)
    a.b(0xC8); a.b(0xBE, 0x00); a.L('p2'); a.b(0x00)
    a.L('w2'); a.abs_(0x2C, PBD2); a.br(0x10, 'w2')
    a.b(0xC0, 0x00); a.br(0xD0, 'loop')
    a.b(0x60)
    code = a.done()
    # p0/p1/p2 zeigen auf das Byte NACH dem High-Byte: korrigieren
    return code, a.lab

if __name__ == '__main__':
    ORG, PAR = 0x2200, 0x2380
    code, lab = assemble(ORG, PAR)
    assert ORG + len(code) <= PAR, len(code)
    print('/* track4040.py: Spurlesen, Code bei $%04X (%d Bytes), Parameter bei $%04X */' % (ORG, len(code), PAR))
    print('#define TRACK4040_ORG 0x%04x' % ORG)
    print('#define TRACK4040_PAR 0x%04x' % PAR)
    print('static const uint8_t track4040_code[] = {')
    for i in range(0, len(code), 12):
        print('    ' + ', '.join('0x%02x' % v for v in code[i:i + 12]) + ',')
    print('};')
