# env.sh - Pfade fuer die psd2sim-Skripte; alles per Umgebung ueberschreibbar
#   VICE   gebauter VICE-Fork (Zweig simlink), mit src/xpet, src/xdrive, src/c1541
#   FW     Firmware (pico/build-emu/petsd2.bin, gebaut mit -DPSD2_EMU=1)
#   WORK   Arbeitsverzeichnis mit Testdaten (test/mkwork.sh)
#   AR6CRT Action Replay 6 als .crt (nur fuer mkwork.sh, kommt als AR6.CRT
#          auf das SD-Abbild; der Datel-Code liegt nicht im Repo)
SIM=${SIM:-$(cd "$(dirname "$0")" && pwd)}
REPO=$(cd "$SIM/../.." && pwd)
VICE=${VICE:-$SIM/../vice-lre-simlink/vice}
FW=${FW:-$REPO/pico/build-emu/petsd2.bin}
WORK=${WORK:-/tmp/psd}
PSD2SIM=${PSD2SIM:-$SIM/target/release/psd2sim}
