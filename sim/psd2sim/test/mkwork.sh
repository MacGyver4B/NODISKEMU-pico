#!/bin/sh
# mkwork.sh - Testdaten fuer run-tests.sh / run-relay-tests.sh in $WORK anlegen
#   AR6CRT=/pfad/Action_Replay_V6.0.crt ./test/mkwork.sh
# Erzeugt: sd.img (64 MB, MBR + FAT16 ab Sektor 2048, mit t.d64/t.d80/t.d82),
# t.d81, big2 (BASIC, 23104 Bytes), ar.d64/ar.d81/ds.d71 (big2 darauf, im
# D71 ueber die Seitengrenze) und sd_ar.img (sd.img + AR6.CRT).
SIM=$(cd "$(dirname "$0")/.." && pwd)
. "$SIM/env.sh"
C=$VICE/src/c1541
set -e
mkdir -p "$WORK" && cd "$WORK"

# SD-Abbild: Partition 1 = FAT16 (0x0e) ab Sektor 2048
rm -f sd.img part.img
mkfs.fat -C -F 16 -n PETSD part.img 64512 > /dev/null
python3 - <<'PY'
import struct
img = bytearray(1024 * 1024) + open('part.img', 'rb').read()
pe = struct.pack('<B3sB3sII', 0, b'\0\0\0', 0x0e, b'\0\0\0', 2048, 129024)
img[446:462] = pe
img[510:512] = b'\x55\xaa'
open('sd.img', 'wb').write(img)
PY
rm -f part.img
for t in d64 d80 d82; do rm -f t.$t; $C -format "test,01" $t t.$t > /dev/null; done
rm -f t.d81; $C -format "t81,81" d81 t.d81 > /dev/null
mcopy -o -i sd.img@@1M t.d64 t.d80 t.d82 ::/

# Testdatei: 300 REM-Zeilen, feste Saat
python3 - <<'PY'
import random
random.seed(9)
z = 'abcdefghijklmnopqrstuvwxyz0123456789'
L = [f'{10+i*10} rem ' + ''.join(random.choice(z) for _ in range(70)) for i in range(300)]
open('big2.bas', 'w').write('\n'.join(L) + '\n')
PY
$VICE/src/tools/petcat/petcat -w40 -l 0401 -o big2 -- big2.bas

rm -f ar.d64 ar.d81 ds.d71
$C -format "ar6,01" d64 ar.d64 -write big2 big2 > /dev/null
$C -format "ar81,01" d81 ar.d81 -write big2 big2 > /dev/null
$C -format "ds,01" d71 ds.d71 > /dev/null
for i in 1 2 3 4 5 6 7; do $C -attach ds.d71 -write big2 fill$i > /dev/null; done
$C -attach ds.d71 -write big2 big2 > /dev/null

cp sd.img sd_ar.img
if [ -n "$AR6CRT" ]; then
  mcopy -o -i sd_ar.img@@1M "$AR6CRT" ::AR6.CRT
else
  echo "AR6CRT nicht gesetzt: sd_ar.img ohne AR6.CRT (Relais nur Standardprotokoll)"
fi
echo "Testdaten in $WORK"
