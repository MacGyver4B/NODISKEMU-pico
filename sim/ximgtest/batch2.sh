#!/bin/sh
cd /home/claude/nodiskemu-pico/sim/ximgtest
C=/home/claude/vice-lre-simlink/vice/src/c1541
chk() { if cmp -s "$1" "$2"; then echo "  => IDENTISCH"; else echo "  => ABWEICHUNG $(cmp -l "$1" "$2" | wc -l) Bytes"; fi; }
run() { pkill -9 -f xpet 2>/dev/null; sleep 1; echo "== $*"; timeout 2400 ./run.sh "$@" 2>&1 | tail -1; }
for sk in 1 2; do
  $C -format "leer,ab" d82 /tmp/b82.d82 >/dev/null 2>&1
  XIMG_TYPE=82 XIMG_SKEW=$sk run 8250 /tmp/b82.d82 wtrack /tmp/demo82.d82; sleep 1; chk /tmp/b82.d82 /tmp/demo82.d82
done
cp /tmp/demo80.d80 /tmp/s80.d80
XIMG_TYPE=80 run 8050 /tmp/s80.d80 track /tmp/o80.d80; chk /tmp/o80.d80 /tmp/demo80.d80
$C -format "leer,ab" d80 /tmp/b80.d80 >/dev/null 2>&1
XIMG_TYPE=80 run 8050 /tmp/b80.d80 wtrack /tmp/demo80.d80; sleep 1; chk /tmp/b80.d80 /tmp/demo80.d80
$C -format "leer,ab" d80 /tmp/c80.d80 >/dev/null 2>&1
XIMG_TYPE=80 run 8250 /tmp/c80.d80 wtrack /tmp/demo80.d80; sleep 1; chk /tmp/c80.d80 /tmp/demo80.d80
cp /tmp/demo80.d80 /tmp/s80b.d80
XIMG_TYPE=80 run 8250 /tmp/s80b.d80 track /tmp/o80b.d80; chk /tmp/o80b.d80 /tmp/demo80.d80
pkill -9 -f xpet
echo FERTIG
