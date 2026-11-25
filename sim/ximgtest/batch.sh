#!/bin/sh
cd /home/claude/nodiskemu-pico/sim/ximgtest
C=/home/claude/vice-lre-simlink/vice/src/c1541
chk() { if cmp -s "$1" "$2"; then echo "  => IDENTISCH"; else echo "  => ABWEICHUNG $(cmp -l "$1" "$2" | wc -l) Bytes"; fi; }
run() { pkill -f xpet 2>/dev/null; sleep 1; echo "== $*"; timeout 1800 ./run.sh "$@" 2>&1 | grep -v "track"; }
# 2031 unformatiert
python3 -c "open('/tmp/unf.d64','wb').write(bytes(174848)+bytes([3])*683)"
run 2031 /tmp/unf.d64 restore /home/claude/imager/t4040.d64; sleep 1; head -c 174848 /tmp/unf.d64 > /tmp/unf_c.d64; chk /tmp/unf_c.d64 /home/claude/imager/t4040.d64
# D82 -> 4040
$C -format "leer,ab" d64 /tmp/tgt.d64 >/dev/null 2>&1
run 4040 /tmp/tgt.d64 restore /tmp/demo82.d82
# 8050 backup + restore
cp /tmp/demo80.d80 /tmp/s80.d80
run 8050 /tmp/s80.d80 backup /tmp/o80.d80; chk /tmp/o80.d80 /tmp/demo80.d80
$C -format "leer,ab" d80 /tmp/b80.d80 >/dev/null 2>&1
run 8050 /tmp/b80.d80 restore /tmp/demo80.d80 -f n -V y; sleep 1; chk /tmp/b80.d80 /tmp/demo80.d80
# 8250 backup + restore
cp /tmp/demo82.d82 /tmp/s82.d82
run 8250 /tmp/s82.d82 backup /tmp/o82.d82; chk /tmp/o82.d82 /tmp/demo82.d82
$C -format "leer,ab" d82 /tmp/b82.d82 >/dev/null 2>&1
run 8250 /tmp/b82.d82 restore /tmp/demo82.d82 -f n -V y; sleep 1; chk /tmp/b82.d82 /tmp/demo82.d82
# D80 auf 8250 (8050-Diskette)
$C -format "leer,ab" d80 /tmp/c80.d80 >/dev/null 2>&1
run 8250 /tmp/c80.d80 restore /tmp/demo80.d80 -V y; sleep 1; chk /tmp/c80.d80 /tmp/demo80.d80
pkill -f xpet
echo FERTIG
