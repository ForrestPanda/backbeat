#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""fm1-drummer: hear a song on the computer, played by the pedal, before there is an FM-1.

    tools/fm1_render.py tests/example-song.json
    tools/fm1_render.py tests/example-song.json --play "INTRO:2 VERSE:8 CHORUS:8 SOLO:8 CHORUS:8 OUTRO"
    tools/fm1_render.py songs/black.json --play "INTRO:2, VERSE 1:4, PRE - CHORUS:4, CHORUS:4"   # commas: names with spaces
    tools/fm1_render.py tests/example-song.json --kit 7 --out take.wav --listen

The play list names the sections (or their numbers) with the bars each plays; the next one is asked for
on beat 3 of the last bar, as a foot would, and lands when the section's pattern ends (its last bar the
fill), so an entry's bars round up to its pattern's: VERSE:2 with a 4-bar verse plays four. A last entry without
bars is the ending: that section once, then the stop on the bar. Without --play: every section in order,
2 bars each (the song's outro, if one is flagged, as the ending). The render is the firmware's own drum
engine, sequencer and pedal (tests/song_render.c, built here on first use; needs a ./build.sh before,
for build/gen). --kit overrides the song's kit (the index in the device's KIT list), --bpm its tempo.
--usrkit plays it with a kit made by tools/mkkit.py --image. --listen plays the WAV with `play` (sox)."""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import fm1_song  # noqa: E402


def build_renderer():
    exe = os.path.join(ROOT, "build", "host", "song_render")
    src = os.path.join(ROOT, "tests", "song_render.c")
    if not os.path.isdir(os.path.join(ROOT, "build", "gen")):
        sys.exit("no build/gen: run ./build.sh once (the generated tables)")
    if not os.path.exists(exe) or os.path.getmtime(exe) < max(os.path.getmtime(src),
                                                              os.path.getmtime(os.path.join(ROOT, "firmware", "src", "sng.c"))):
        os.makedirs(os.path.dirname(exe), exist_ok=True)
        cmd = ["cc", "-O2", "-w", "-Ibuild/gen", "-Ifirmware/src", "-o", exe, src, "-lm"]
        r = subprocess.run(cmd, cwd=ROOT)
        if r.returncode:
            sys.exit("building the renderer failed")
    return exe


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("json", help="the song (JSON, as tools/fm1_song.py takes it)")
    ap.add_argument("--play", help='the play list: "INTRO:2 VERSE:8 ... OUTRO"')
    ap.add_argument("--kit", type=int, help="override the song's kit")
    ap.add_argument("--usrkit", help="a user kit image (tools/mkkit.py --image): played as KIT USR3+4")
    ap.add_argument("--bpm", type=int, help="override the song's tempo")
    ap.add_argument("--count-in", type=int, metavar="BARS", help="the pedal's count-in: bars of clicks before the start (0, 1, 2)")
    ap.add_argument("--out", help="the WAV (default: build/host/<name>.wav)")
    ap.add_argument("--tail", type=float, default=1.5, help="seconds after the stop (default 1.5)")
    ap.add_argument("--listen", action="store_true", help="play it (sox)")
    a = ap.parse_args()
    import json
    with open(a.json) as f:
        spec = json.load(f)
    blob = fm1_song.pack(spec)
    names = [str(s.get("name", f"PART {i + 1}"))[:12].upper() for i, s in enumerate(spec["sections"])]
    if a.play:
        entries = []
        toks = [x.strip() for x in a.play.split(",")] if "," in a.play else a.play.split()   # (commas: names with spaces)
        for tok in toks:
            if not tok:
                continue
            name, _, bars = tok.rpartition(":") if ":" in tok else (tok, "", "")
            key = name.upper()
            if key.isdigit():
                idx = int(key)
            elif key in names:
                idx = names.index(key)
            else:
                sys.exit(f"no section {name!r}; sections: {' '.join(names)}")
            entries.append(f"{idx}:{bars}" if bars else str(idx))
    else:
        outro = [i for i, s in enumerate(spec["sections"]) if s.get("outro")]
        entries = [f"{i}:2" for i in range(len(names)) if i not in outro] + [str(outro[0])] if outro else \
                  [f"{i}:2" for i in range(len(names))]
    base = os.path.splitext(os.path.basename(a.json))[0]
    out = a.out or os.path.join(ROOT, "build", "host", base + ".wav")
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    bin_path = os.path.join(ROOT, "build", "host", base + ".sng")
    with open(bin_path, "wb") as f:
        f.write(blob)
    env = dict(os.environ, TAIL=str(a.tail))
    if a.kit is not None:
        env["KIT"] = str(a.kit)
    if a.bpm is not None:
        env["BPM"] = str(a.bpm)
    if a.count_in:
        env["COUNT_IN"] = str(a.count_in)
    if a.usrkit:
        env["USRKIT"] = os.path.abspath(a.usrkit)
    exe = build_renderer()
    r = subprocess.run([exe, bin_path, out, " ".join(entries)], env=env, cwd=ROOT)
    if r.returncode:
        sys.exit(r.returncode)
    if a.listen:
        subprocess.run(["play", "-q", out])


if __name__ == "__main__":
    main()
