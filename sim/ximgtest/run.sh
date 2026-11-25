#!/bin/sh
# run.sh drivetype diskimage mode image [ximgtest-optionen]
#   startet xpet (PET wartet bei READY) mit dem Laufwerk als Geraet 9 am
#   simlink-Bus und ximgtest als Controller
DT=$1; DISK=$2; MODE=$3; IMG=$4; shift 4
V=${VICE:-/home/claude/vice-lre-simlink/vice}
BUS=ximg$$
rm -f /dev/shm/simlink-$BUS
xvfb-run -a $V/src/xpet -default -directory $V/data +sound -model 8032 -drive8type 0 +busdevice8 \
  -drive9type $DT -drive9truedrive -9 "$DISK" -ieeesimlink $BUS -warp -ieeesimlinkstep 8 > xpet_$BUS.log 2>&1 &
X=$!
./ximgtest -d 9 "$@" $BUS $MODE "$IMG"
R=$?
pkill -P $X 2>/dev/null; kill $X 2>/dev/null; sleep 1; pkill -f "ieeesimlink $BUS" 2>/dev/null
rm -f /dev/shm/simlink-$BUS xpet_$BUS.log
exit $R
