#!/bin/sh
# Usage: shots.sh <stage-number> <label> <full-map> <cutaway-map>
set -e
cd "$(dirname "$0")"
n=$1; label=$2
python tb.py load "$4" >/dev/null
python tb.py view 520 250 2300 90 -66 "${n}_${label}_overview.png" >/dev/null
python tb.py load "$3" >/dev/null
python tb.py view 860 930 330 128 -28 "${n}_${label}_sluice_hall.png" >/dev/null
python tb.py view 1260 2016 160 42 -22 "${n}_${label}_yard.png" >/dev/null
python tb.py view -360 1990 280 132 -24 "${n}_${label}_relay_arena.png" >/dev/null
python tb.py view 220 420 170 238 -18 "${n}_${label}_arrival.png" >/dev/null
echo "stage $n captured"
if [ -n "$5" ]; then
  python tb.py view -176 1400 120 215 -30 "${n}_${label}_pump_room.png" >/dev/null
  python tb.py view 600 1600 -360 0 -12 "${n}_${label}_underworks_channel.png" >/dev/null
  python tb.py view 1060 2340 260 205 -30 "${n}_${label}_control_tower.png" >/dev/null
  python tb.py view 700 1640 220 200 -12 "${n}_${label}_catwalk_return.png" >/dev/null
  echo "extra views captured"
fi
