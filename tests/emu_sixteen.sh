#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# 16 sections a song on the FM-1 emulator (fm1-emu next to my_fw; ./build.sh first): the firmware image itself, with
# tests/data/sixteen.json in slot 1. SAVE, white key 1 (the song, LIVE: four rows of tiles), white key 10 (stopped:
# section 10 starts), white key 16 (cued for its end), OCT+ held + white key 3 (CHORUS on the next bar; the state
# line says so), the pedal's CC 20 15 (OUTRO at the end) and CC 27 12 (HALF TIME on the next bar), the ending.
# Screens: build/emu/lcd_<ms>.png; the sound: build/emu/sixteen.wav. Not part of run_tests.sh (minutes of emulation).
set -e
cd "$(dirname "$0")/.."
python3 tools/fm1_emu.py tests/data/sixteen.json --count-in 0 \
  --keys "3000:SAVE,3800:n0,4600:n16,6000:n26,8000-8800:OCTUP,8300:n4,9600:midi:bf140f,10000:midi:bf1b0c,11500:midi:bf167f" \
  --snap "4400,5600,6400,7200,8200,8500,9300,10200,11000,12000,15000" --ms 15500 --wav build/emu/sixteen.wav
# the other row counts: 3 sections (one row, whole names) and 11 (three rows)
for s in three eleven; do
  python3 tools/fm1_emu.py "tests/data/$s.json" --count-in 0 --keys "3000:SAVE,3800:n0,4600:PLAY" --snap 5400 --ms 5600
  cp build/emu/lcd_5400.png "build/emu/lcd_$s.png"
done
