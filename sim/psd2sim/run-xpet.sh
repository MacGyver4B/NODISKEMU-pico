#!/bin/sh
# run-xpet.sh name zyklen keybuf [sdimg] - petSD2-Firmware in psd2sim gegen xpet
N=$1; C=$2; K=$3; IMG=${4:-/tmp/psd/sd.img}
SIM=$(cd "$(dirname "$0")" && pwd)
. "$SIM/env.sh"
cd "$WORK"
cp "$IMG" sd_$N.img
rm -f /dev/shm/simlink-$N
timeout 1800 "$PSD2SIM" -s sd_$N.img "$FW" $N > b_$N.log 2>&1 &
B=$!
sleep 0.5
timeout 1800 $VICE/src/xpet -default -directory $VICE/data +sound -model 4032 -drive8type 0 +busdevice8 \
    -ieeesimlink $N -warp -limitcycles $C -exitscreenshot s_$N.png -keybuf "$K" > x_$N.log 2>&1
wait $B
