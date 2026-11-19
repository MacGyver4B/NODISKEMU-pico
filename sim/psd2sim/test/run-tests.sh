#!/bin/sh
# run-tests.sh - Regressionstest petSD2-Firmware gegen xpet (psd2sim)
#   FW=... ./run-tests.sh      Ergebnis: Zeilen mit FEHLER, Ausgang 1 bei Fehlern
# Erwartet $WORK/sd.img und $WORK/t.d81 (test/mkwork.sh).
# Bekannt (NODISKEMU selbst, auch petSD+): REL auf FAT ohne x00 laesst sich
# nicht ohne Satzlaenge wieder oeffnen ("REL NEU"), REL in Images fehlt ganz.
T=$(dirname "$0"); T=$(cd "$T" && pwd)
SIM=$(cd "$T/.." && pwd)
. "$SIM/env.sh"
P=$VICE/src
$P/tools/petcat/petcat -w40 -l 0401 -o $T/psdtest.prg -- $T/psdtest.bas || exit 2
cd "$WORK" && cp sd.img sdt.img && mcopy -o -i sdt.img@@1M t.d81 $T/psdtest.prg ::/ || exit 2
$T/../run-xpet.sh tst 60000000 'load"psdtest*",8\nrun\n' sdt.img > /dev/null 2>&1
mtype -i sd_tst.img@@1M ::TESTLOG > log.txt 2>/dev/null || { echo "kein Testprotokoll"; exit 1; }
n=$(tr '\r' '\n' < log.txt | grep -c ': ')
bad=$(tr '\r' '\n' < log.txt | grep ': FEHLER' | grep -v '^REL NEU:')
echo "$n Tests"; [ -z "$bad" ] && { echo "alle ok (REL NEU bekannt)"; exit 0; }
echo "$bad"; exit 1
