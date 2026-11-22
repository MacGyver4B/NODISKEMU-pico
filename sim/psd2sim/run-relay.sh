#!/bin/sh
# run-relay.sh name zyklen keybuf d64 [drivetype] - xpet + petSD2 (psd2sim) + xdrive
N=$1; C=$2; K=$3; D=$4; TY=${5:-1541}
SIM=$(cd "$(dirname "$0")" && pwd)
. "$SIM/env.sh"
cd "$WORK"
cp ${SDIMG:-sd.img} sd_$N.img
cp "$D" d_$N.img
rm -f /dev/shm/simlink-${N}i /dev/shm/simlink-${N}c
timeout 2400 "$PSD2SIM" $PSD2OPT -s sd_$N.img "$FW" ${N}i ${N}c > b_$N.log 2>&1 &
B=$!
sleep 0.5
timeout 2400 $VICE/src/xdrive -default -directory $VICE/data +sound $XDOPT -iecsimlink ${N}c -drive8type $TY \
    -8 d_$N.img -warp -limitcycles $((C + 3000000)) > d_$N.log 2>&1 &
X=$!
sleep 0.5
timeout 2400 $VICE/src/xpet -default -directory $VICE/data +sound -model 4032 $XARGS -drive8type 0 +busdevice8 \
    -ieeesimlink ${N}i -warp -limitcycles $C -exitscreenshot s_$N.png -keybuf "$K" > x_$N.log 2>&1
wait $X
sleep 0.5
kill $B 2>/dev/null; wait $B 2>/dev/null
