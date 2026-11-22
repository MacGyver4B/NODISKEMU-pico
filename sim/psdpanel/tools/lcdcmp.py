#!/usr/bin/env python3
"""lcdcmp.py - stehende LCD-Bildschirme zweier Laeufe (PSD2_LCDDUMP) vergleichen

  lcdcmp.py avr.log pico.log [min_ms]

Ein Bildschirm zaehlt, wenn er mindestens min_ms (Vorgabe 100) steht;
halb gezeichnete Zwischenstaende fallen so heraus. Ausgabe: beide Folgen
nebeneinander, Unterschiede markiert."""
import re, sys

def screens(path, min_ms):
    t = open(path, encoding='utf-8', errors='replace').read()
    ev = []
    for m in re.finditer(r'LCD bei (\d+) ms[^\n]*\n\+-+\+\n((?:\|.*\|\n){4})', t):
        ev.append((int(m.group(1)), tuple(l[1:-1] for l in m.group(2).splitlines())))
    keys = [(int(m.group(2)), m.group(1).strip()) for m in re.finditer(r'Tasten (.*?) bei (\d+) ms', t)]
    out = []
    for i, (ms, s) in enumerate(ev):
        end = ev[i + 1][0] if i + 1 < len(ev) else None
        if end is None or end - ms >= min_ms:
            if not out or out[-1][1] != s:
                out.append((ms, s))
    return out, keys

def main():
    a, b = sys.argv[1], sys.argv[2]
    min_ms = int(sys.argv[3]) if len(sys.argv) > 3 else 100
    sa, ka = screens(a, min_ms)
    sb, kb = screens(b, min_ms)
    print(f'{a}: {len(sa)} Bildschirme   {b}: {len(sb)} Bildschirme')
    print('Tasten A:', ' '.join(f'{k}@{t}' for t, k in ka) or '-')
    print('Tasten B:', ' '.join(f'{k}@{t}' for t, k in kb) or '-')
    n = max(len(sa), len(sb))
    diff = 0
    for i in range(n):
        x = sa[i] if i < len(sa) else (None, ('',) * 4)
        y = sb[i] if i < len(sb) else (None, ('',) * 4)
        same = x[1] == y[1]
        diff += not same
        print(f'\n#{i+1} {"gleich" if same else "ANDERS"}   A {x[0]} ms   B {y[0]} ms')
        for la, lb in zip(x[1], y[1]):
            print(f'  |{la:20}|  {" " if la == lb else "#"}  |{lb:20}|')
    print(f'\n{diff} von {n} Bildschirmen verschieden')

main()
