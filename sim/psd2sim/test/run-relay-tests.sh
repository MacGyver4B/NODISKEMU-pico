#!/bin/sh
# run-relay-tests.sh [fall...] - Relais-Regression petSD2 (xpet + psd2sim + xdrive)
# Faelle: std1541 ar1541 ar1571 ar1581 absent; ohne Angabe alle.
# Ergebnis je Fall in $WORK/ergebnis.txt (Testdaten: test/mkwork.sh); PSD2_COARSE=8 fuer feines Raster.
SIM=$(cd "$(dirname "$0")/.." && pwd)
. "$SIM/env.sh"
R=$SIM/run-relay.sh
OUT=$WORK/ergebnis.txt
cd "$WORK" || exit 2
K='fori=1to2000:next\nload"big2",9\nsave"@:chk",8\nsave"neu",9\n10 open1,9,15:input#1,a,b$:close1:printa;b$\nrun\nload"nix",9\n'

check() {   # name image
  n=$1; img=$2; ok=1
  rm -f chk_$n.bin neu_$n.bin
  mcopy -n -o -i sd_$n.img@@1M ::CHK chk_$n.bin 2>/dev/null
  $VICE/src/c1541 -attach d_$n.img -read neu neu_$n.bin >/dev/null 2>&1
  l=$(cmp -s big2 chk_$n.bin && echo ok || echo FEHLER)
  s=$(cmp -s big2 neu_$n.bin && echo ok || echo FEHLER)
  info=$(grep -o 'relay AR6 [0-9]*: [0-9]* Bytes in [0-9]* ms ([0-9]* B/s)\|relay SAVE [A-Za-z0-9]*: [0-9]* Bytes in [0-9]* ms\|nicht moeglich[^,]*' b_$n.log | tr '\n' ';')
  echo "$(date +%H:%M) $n: LOAD $l, SAVE $s | $info" >> $OUT
}

run() {     # name image drivetype sdimg [zyklen]
  SDIMG=$4 $R $1 ${5:-50000000} "$K" $2 $3 > /dev/null 2>&1
  check $1 $2
}

[ $# -eq 0 ] && set -- std1541 ar1541 ar1571 ar1581 absent
for c in "$@"; do
  case $c in
    std1541) run std1541 ar.d64 1541 sd.img 190000000 ;;
    ar1541)  run ar1541 ar.d64 1541 sd_ar.img ;;
    ar1571)  run ar1571 ar.d64 1571 sd_ar.img ;;
    ar1581)  run ar1581 ar.d81 1581 sd_ar.img ;;
    std1581) run std1581 ar.d81 1581 sd.img 190000000 ;;
    ds1571)  run ds1571 ds.d71 1571 sd_ar.img 190000000 ;;
    ss1571)  run ss1571 ar.d64 1571 sd_ar.img ;;
    absent)  SDIMG=sd_ar.img $R absent 20000000 'fori=1to2000:next\nload"big2",9\nsave"neu",9\n' ar.d64 0 > /dev/null 2>&1
             echo "$(date +%H:%M) absent: siehe s_absent.png" >> $OUT ;;
  esac
done
