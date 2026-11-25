#!/usr/bin/env python3
# Laufwerkscode fuer den DOS-Prozessor der 4040 (Messstufe 1):
# sendet 256 Bytes aus einem Puffer mit Zwei-Draht-Handshake.
#   Floppy: Byte auf DIO, DAV umschalten
#   Controller: Byte lesen, NRFD umschalten (Quittung)
# Aufruf per M-E, laeuft mit gesperrten Interrupts (IDLE: SEI vor PARSXQ).
# Ausgabe: C-Array fuer ximgtest
import sys

IEEEDO, PAD2, PBD2 = 0x0202, 0x0280, 0x0282
DAVO, NRFDI = 0x10, 0x80
TMP = 0x02          # TEMP (DOS-Scratch in der Zero-Page)

def assemble(org, src_buf):
    c = []
    lab = {}
    fix = []
    def b(*x): c.extend(x)
    def w(v): c.extend([v & 255, v >> 8])
    # DAV freigeben, Ausgangszustand der Quittung merken
    b(0xAD); w(PAD2); b(0x09, DAVO); b(0x8D); w(PAD2)      # LDA PAD2 / ORA #DAVO / STA PAD2
    b(0xAD); w(PBD2); b(0x29, NRFDI); b(0x85, TMP)          # LDA PBD2 / AND #$80 / STA TMP
    b(0xA0, 0x00)                                           # LDY #0
    lab['loop'] = len(c)
    b(0xB9); w(src_buf)                                     # LDA buf,Y
    b(0x49, 0xFF); b(0x8D); w(IEEEDO)                       # EOR #$FF / STA IEEEDO
    b(0xAD); w(PAD2); b(0x49, DAVO); b(0x8D); w(PAD2)       # DAV umschalten
    lab['wait'] = len(c)
    b(0xAD); w(PBD2); b(0x29, NRFDI); b(0xC5, TMP)          # LDA PBD2 / AND #$80 / CMP TMP
    b(0xF0); fix.append((len(c), 'wait')); b(0)             # BEQ wait
    b(0x85, TMP)                                            # STA TMP
    b(0xC8)                                                 # INY
    b(0xD0); fix.append((len(c), 'loop')); b(0)             # BNE loop
    b(0xA9, 0xFF); b(0x8D); w(IEEEDO)                       # Datenleitungen frei
    b(0xAD); w(PAD2); b(0x09, DAVO); b(0x8D); w(PAD2)       # DAV frei
    b(0x60)                                                 # RTS
    for pos, l in fix:
        c[pos] = (lab[l] - (pos + 1)) & 255
    return c

def assemble2(org, src_buf):
    """Version 2: zwei Bytes je Durchlauf, naechstes Byte vorgeladen (X),
    Daten roh (Controller invertiert), DAV-Werte fest in der Zero-Page."""
    PLO, PHI = TMP, TMP + 1
    c = []; lab = {}; fix = []
    def b(*x): c.extend(x)
    def w(v): c.extend([v & 255, v >> 8])
    b(0xAD); w(PAD2); b(0x09, DAVO); b(0x8D); w(PAD2); b(0x85, PHI)   # DAV frei, PHI
    b(0x29, 0xFF ^ DAVO); b(0x85, PLO)                                 # PLO = DAV low
    b(0xA0, 0x00); b(0xAE); w(src_buf)                                 # LDY #0 / LDX buf
    lab['loop'] = len(c)
    b(0x8E); w(IEEEDO); b(0xA5, PLO); b(0x8D); w(PAD2)                 # STX DO / DAV low
    b(0xC8); b(0xBE); w(src_buf)                                       # INY / LDX buf,Y
    lab['w1'] = len(c)
    b(0x2C); w(PBD2); b(0x30); fix.append((len(c), 'w1')); b(0)        # BIT PBD2 / BMI w1 (NRFD low?)
    b(0x8E); w(IEEEDO); b(0xA5, PHI); b(0x8D); w(PAD2)                 # STX DO / DAV high
    b(0xC8); b(0xBE); w(src_buf)                                       # INY / LDX buf,Y
    lab['w2'] = len(c)
    b(0x2C); w(PBD2); b(0x10); fix.append((len(c), 'w2')); b(0)        # BIT PBD2 / BPL w2 (NRFD high?)
    b(0xC0, 0x00); b(0xD0); fix.append((len(c), 'loop')); b(0)         # CPY #0 / BNE loop
    b(0xA9, 0xFF); b(0x8D); w(IEEEDO)                                  # Daten frei
    b(0x60)
    for pos, l in fix:
        c[pos] = (lab[l] - (pos + 1)) & 255
    return c

if __name__ == '__main__':
    v2 = len(sys.argv) > 1 and sys.argv[1] == '2'
    code = (assemble2 if v2 else assemble)(0x2200, 0x2100)
    print('#define FAST4040_RAW %d' % (1 if v2 else 0))
    print('/* fast4040.py: 256 Bytes aus $2100 (Puffer 4) senden, Code bei $2200 (Puffer 5), %d Bytes */' % len(code))
    print('static const uint8_t fast4040_send[] = {')
    for i in range(0, len(code), 12):
        print('    ' + ', '.join('0x%02x' % v for v in code[i:i+12]) + ',')
    print('};')
