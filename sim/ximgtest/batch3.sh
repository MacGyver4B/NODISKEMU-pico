#!/bin/sh
cd /home/claude/nodiskemu-pico/sim/ximgtest
C=/home/claude/vice-lre-simlink/vice/src/c1541
T=/home/claude/imager/t4040.d64
chk() { if cmp -s "$1" "$2"; then echo "  => IDENTISCH"; else echo "  => ABWEICHUNG $(cmp -l "$1" "$2" 2>/dev/null | wc -l) Bytes"; fi; }
run() { pkill -9 -f xpet 2>/dev/null; sleep 1; echo "== $*"; timeout 2400 ./run.sh "$@" 2>&1 | grep -E "^(backup|restore|time|note|question)"; }
cp $T /tmp/src.d64
run 4040 /tmp/src.d64 backup /tmp/m1.d64; chk /tmp/m1.d64 $T
$C -format "leer,ab" d64 /tmp/m2.d64 >/dev/null 2>&1
run 4040 /tmp/m2.d64 restore $T -f n; sleep 1; chk /tmp/m2.d64 $T
$C -format "leer,ab" d64 /tmp/m3.d64 >/dev/null 2>&1
run 4040 /tmp/m3.d64 restore $T -f y; sleep 1; chk /tmp/m3.d64 $T
$C -format "leer,ab" d64 /tmp/m4.d64 >/dev/null 2>&1
run 4040 /tmp/m4.d64 restore /tmp/demo82.d82
run 4040 /tmp/src.d64 backup /tmp/m5.d64 -S; chk /tmp/m5.d64 $T
cp /tmp/demo80.d80 /tmp/s80.d80
run 8050 /tmp/s80.d80 backup /tmp/m6.d80; chk /tmp/m6.d80 /tmp/demo80.d80
$C -format "leer,ab" d80 /tmp/m7.d80 >/dev/null 2>&1
run 8050 /tmp/m7.d80 restore /tmp/demo80.d80 -f n; sleep 1; chk /tmp/m7.d80 /tmp/demo80.d80
cp /tmp/demo82.d82 /tmp/s82.d82
run 8250 /tmp/s82.d82 backup /tmp/m8.d82; chk /tmp/m8.d82 /tmp/demo82.d82
$C -format "leer,ab" d82 /tmp/m9.d82 >/dev/null 2>&1
run 8250 /tmp/m9.d82 restore /tmp/demo82.d82 -f n; sleep 1; chk /tmp/m9.d82 /tmp/demo82.d82
$C -format "leer,ab" d80 /tmp/m10.d80 >/dev/null 2>&1
run 8250 /tmp/m10.d80 restore /tmp/demo80.d80; sleep 1; chk /tmp/m10.d80 /tmp/demo80.d80
pkill -9 -f xpet
echo FERTIG
