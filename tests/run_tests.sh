#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Host tests (no hardware). Run from the repo root after ./build.sh:
#   tests/run_tests.sh
#
# Regression suite (tests/regress.c, tests/target_budget.py; details at the top of regress.c):
#   golden renders  every drum kit, the drum track's FX (sends, INSERT, FILTER), the master: one hash each in
#                   tests/golden.txt. A change of the sound fails with the list of renders.
#   health          clipping, DC, peak level, silence at the end.
#   CPU             cost / sample per render, relative to the idle + drums mix (tests/cpu_baseline.txt,
#                   +25 %; counted by the kernel or under callgrind, else timed at +35 %), ns printed;
#                   target: loop instructions of the render functions in build/felucca.dis
#                   (tests/target_budget.txt, +10 %; exact, static).
# After an intended change of the sound: GOLDEN_UPDATE=1 sh tests/run_tests.sh, review the diff
# of tests/golden.txt, commit it with the change. After an intended change of the cost (or a new
# compiler): BUDGET_UPDATE=1 (rewrites cpu_baseline.txt and target_budget.txt). VERBOSE=1: every render.
set -e
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
cd "$(dirname "$0")/.."
OUT=build/host
mkdir -p "$OUT"
CC="${CC:-cc} -O1 -Wall -Wno-unused-function"
fail=0
run() { echo "== $1"; name=$1; shift; "$@" || { echo "FAILED: $name"; fail=1; failed="$failed, $name"; }; }

[ -f build/felucca.fwsc ] || { echo "run ./build.sh first"; exit 1; }
# the generated tables (tools/build.py generate() makes them too; no Pillow needed)
mkdir -p build/gen
[ build/gen/felucca_tables.h -nt tools/gen_tables.py ] || python3 tools/gen_tables.py build/gen/felucca_tables.h

$CC -o "$OUT/storage_test" tests/storage_test.c
run "flash storage (A/B, torn writes)" "$OUT/storage_test"

$CC -o "$OUT/recovery_test" tests/recovery_test.c
run "application USB recovery and boot-loop guard" "$OUT/recovery_test"

$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/pedal_test" tests/pedal_test.c -lm
run "pedal: a song's sections, next / previous, stop, tap tempo, fills, endings, the count-in over TRS MIDI" "$OUT/pedal_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/song_test" tests/song_test.c -lm
run "songs: packed form, PC loads one, its sections on the bar, the outro, PC 127 unloads" "$OUT/song_test"
run "songs: a section is whole bars of its time signature (tools/fm1_song.py bar_fit)" python3 tests/song_bars_test.py
run "songs: the example packed by tools/fm1_song.py passes the firmware's check" sh -c \
    "python3 tools/fm1_song.py tests/example-song.json --out $OUT/example-song.bin >/dev/null && $OUT/song_test $OUT/example-song.bin"
run "songs: the example played by the pedal and rendered (tools/fm1_render.py, tests/song_render.c)" sh -c \
    "python3 tools/fm1_render.py tests/example-song.json --out $OUT/example-song.wav --tail 0.5 >/dev/null && [ -s $OUT/example-song.wav ]"
run "kit: tools/mkkit.py from the CC0 recordings into a USR3+4 image, the example rendered with it" sh -c \
    "python3 tools/mkkit.py STUDIO assets/samples-cc0/KIT --image $OUT/studio-kit.img >/dev/null && python3 tools/fm1_render.py tests/example-song.json --usrkit $OUT/studio-kit.img --out $OUT/example-studio.wav --tail 0.5 | grep -q 'USR3+4'"
if [ -f ../../kits/avl-drumkits/Red_Zeppelin_5pc.sfz ] && command -v sox >/dev/null 2>&1; then
    run "kit: tools/mkkit.py --sfz on AVL Drumkits' Red Zeppelin (16 lanes from the hardest layers)" sh -c \
        "python3 tools/mkkit.py REDZEP --sfz ../../kits/avl-drumkits/Red_Zeppelin_5pc.sfz --image $OUT/redzep.img | grep -q '16 lanes'"
else
    echo "== skip the Red Zeppelin kit (no ../../kits/avl-drumkits, or no sox)"
fi
run "songs: tools/gp2song.py on MIDI files written by the test (type 0 and 1, markers, 7/8, flams, the drums found)" python3 tests/midi2song_test.py
if python3 -c "import guitarpro" 2>/dev/null; then
    run "songs: tools/gp2song.py on a Guitar Pro file written with PyGuitarPro (sections, pattern, fill, flam, accents)" python3 tests/gp2song_test.py
else
    echo "== skip gp2song (no PyGuitarPro: pip install pyguitarpro)"
fi

$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/studio_drums_test" tests/studio_drums_test.c -lm
run "drum lanes, kit audio, metronome, record arm, free take" "$OUT/studio_drums_test" "$OUT/drum-styles.wav"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/seq2_test" tests/seq2_test.c -lm
run "sequencer 2.0: no drift, ratchets, roll, erase / undo, ghost / hard, chords, mute / solo, nudge, locks" "$OUT/seq2_test"

$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/drumkit_test" tests/drumkit_test.c -lm
run "synthesised drum kits: every kit x sound bounded, audible, finite, levels, cost" "$OUT/drumkit_test" "$OUT/drum-kits.wav" "$OUT/drum-kits.txt"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o "$OUT/userkit_test" tests/userkit_test.c -lm
run "user drum kits (KIT USR1..USR3): a user slot's sounds on the drum lanes" "$OUT/userkit_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/dsyn_test" tests/dsyn_test.c -lm
run "SYN1..SYN4 drum kits and the editor's DRUM SYNTH commands (v10)" "$OUT/dsyn_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/font_test" tests/font_test.c
run "the screen font at 1 bit a pixel: every glyph, the same pixels as 2.5" "$OUT/font_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/fx251_test" tests/fx251_test.c -lm
run "after Felucca 1.4 / 1.5: the drums INSERT, mutes, MIDI LEARN, TEXTURE, limiter, THROW" "$OUT/fx251_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/gate_test" tests/gate_test.c -lm
run "the audio gate: closed 3 s after silence, opened by a hit, a key, a TRS note, the transport, TEXTURE, DUST" "$OUT/gate_test"

$CC -O2 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o "$OUT/ui_pages_test" tests/ui_pages_test.c -lm
run "live UI: pages, layers (fx, steps, erase, roll, key, mix), holds, drums, REC, fuzz" "$OUT/ui_pages_test" "$OUT"
# no divide by 0 (the FM-1 runs with the div0 trap off, hal/fm1_irq.h: a real one would give a wrong value
# silently): the UI fuzz, the sequencer, the projects and a minute of random live use, with UBSan
UBSAN="${CC_UB:-cc} -O1 -w -fsanitize=integer-divide-by-zero -fno-sanitize-recover=integer-divide-by-zero -Ibuild/gen -Ifirmware/src -Ifirmware/hal"
$UBSAN -o "$OUT/ui_pages_ub" tests/ui_pages_test.c -lm && $UBSAN -o "$OUT/seq2_ub" tests/seq2_test.c -lm &&
    $UBSAN -o "$OUT/project_ub" tests/project_test.c -lm && $UBSAN -o "$OUT/soak_ub" tests/soak_test.c -lm || fail=1
mkdir -p "$OUT/ub"
run "no divide by zero (UBSan): UI fuzz, sequencer, projects, a minute of live use" \
    sh -c "'$OUT/ui_pages_ub' '$OUT/ub' >/dev/null && '$OUT/seq2_ub' >/dev/null && '$OUT/project_ub' >/dev/null && '$OUT/soak_ub' 1 >/dev/null && echo 'no divide by zero'"

$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/soak_test" tests/soak_test.c -lm
run "soak: ${SOAK_MIN:-10} minutes of random live use (bounded, no hanging voices, idle after stop)" "$OUT/soak_test" "${SOAK_MIN:-10}"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o "$OUT/stress_test" tests/stress_test.c -lm
run "stress: FONT_L = FONT_S at 2x, hard random use of the drums and the UI, clean stop" "$OUT/stress_test" "${STRESS_FRAMES:-40000}"
# the same under AddressSanitizer + UBSan: any read or write out of bounds stops it (left shifts of negative values
# and the FM6 phase's wrap-around are left out: the DSP's two's complement idioms, as every compiler builds them)
ASAN="${CC_UB:-cc} -O1 -g -w -fsanitize=address,undefined -fno-sanitize=shift-base,signed-integer-overflow -fno-sanitize-recover=all -Ibuild/gen -Ifirmware/src -Ifirmware/hal"
if $ASAN -o "$OUT/stress_asan" tests/stress_test.c -lm 2>/dev/null; then
    run "stress under ASan + UBSan (no access out of bounds)" "$OUT/stress_asan" "${STRESS_ASAN_FRAMES:-15000}" 7
else
    echo "(stress under ASan: this compiler has no AddressSanitizer, skipped)"
fi

$CC -o "$OUT/midi_uart_test" tests/midi_uart_test.c
run "TRS MIDI parser" "$OUT/midi_uart_test"
$CC -Ifirmware/hal -o "$OUT/encoder_test" tests/encoder_test.c
run "knobs: one click = one step (slow, fast, pauses, bounce)" "$OUT/encoder_test"
HALF=$(sed -n 's/^#define HALF_FRAMES \([0-9]*\).*/\1/p' firmware/src/core.h)
$CC -DT_CDC=1 -DHALF_FRAMES=$HALF -o "$OUT/uac_test" tests/uac_test.c -lm
run "USB audio input: descriptors (with CDC), ring and packets" "$OUT/uac_test"
$CC -DT_CDC=0 -DHALF_FRAMES=$HALF -o "$OUT/uac_test_nocdc" tests/uac_test.c -lm
run "USB audio input: descriptors (without CDC), ring and packets" "$OUT/uac_test_nocdc"
$CC -DT_CDC=2 -DHALF_FRAMES=$HALF -o "$OUT/uac_test_seroff" tests/uac_test.c -lm
run "USB audio input: descriptors (CDC built in, menu USB SERIAL OFF), ring and packets" "$OUT/uac_test_seroff"
run "USB SERIAL OFF: the descriptors of a build without CDC, byte for byte" \
    sh -c "[ \"\$(UAC_DUMP=1 '$OUT/uac_test_seroff' | tail -n 2)\" = \"\$(UAC_DUMP=1 '$OUT/uac_test_nocdc' | tail -n 2)\" ] && echo same"
uac_in_app() { ${CC%% *} -E -Ibuild/gen -Ifirmware/hal -Ifirmware/src firmware/src/felucca.c 2>/dev/null | grep -q uac_service; }
run "USB audio input: built into the firmware (FELUCCA_UAC set before usb.c)" uac_in_app

$CC -o "$OUT/ota_test" tests/ota_test.c
run "M-UPGRADE entry" "$OUT/ota_test" build/felucca.fwsc

head -c 200000 build/felucca.bin > "$OUT/old_app.bin"
python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
$CC -o "$OUT/ldr_test" tests/ldr_test.c
run "update loader: other app -> this build" "$OUT/ldr_test" "$OUT/old.fwsc" build/felucca.fwsc

$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/hostsim" tests/hostsim.c -lm
run "DSP render (the groove on the power-on kit)" "$OUT/hostsim" 5 "$OUT/render.wav"
mkdir -p build/tracks_demo
run "TRACKS: the groove, TRS MIDI IN, the cost of the drums" env TRACKS=build/tracks_demo "$OUT/hostsim"
$CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/project_test" tests/project_test.c -lm
run "project formats (DRM1; FUN6 / FUN5 / FUN4 -> their drum track), capture / apply" "$OUT/project_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/regress" tests/regress.c -lm
# the CPU budget: counted by the kernel on macOS; elsewhere under callgrind when valgrind is there (exact, ~45 s;
# SKIP_CPU_VALGRIND=1 to time instead, which is only a rough check)
if [ -z "$SKIP_CPU_VALGRIND" ] && command -v valgrind >/dev/null 2>&1; then export CPU_VALGRIND=1; fi
run "regression: golden renders, health, voices, CPU budget" "$OUT/regress" tests/golden.txt tests/cpu_baseline.txt

run "regression: target cost of the render loops" python3 tests/target_budget.py \
    build/felucca.dis tests/target_budget.txt

run "installer CLI (fm1_install.py) against a simulated FM-1" python3 tests/install_test.py
run "the offline editor (one file, nothing next to it); the offline site (sw.js, manifest)" python3 tests/site_test.py

if command -v node >/dev/null 2>&1; then
    run "web pages: editor protocol, samples, packages, update protocol" node web/test_web.mjs
    rm -rf "$OUT/pwa-site" && python3 web/make_site.py build/felucca.fwsc 2.6 "$OUT/pwa-site" >/dev/null &&
        run "the site without internet in Chromium (skipped without Playwright)" node tests/pwa_test.mjs "$OUT/pwa-site"
else
    echo "== skip web tests (no node)"
fi

[ $fail -eq 0 ] && echo "ALL HOST TESTS PASSED" || { echo "HOST TESTS FAILED:${failed#,}"; exit 1; }
