#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Build the drum samples (the GM kit, set PERC) from WAV files into a C header (firmware/src/samples.c).

Samples are stored as IMA ADPCM (4 bit). Roots come from the file names, loops
from the WAV 'smpl' chunk; the ADPCM state at the loop start is stored so loops
restart exactly.

Sources:
  assets/samples-cc0/   CC0 samples (CREDITS.txt there): PIANO (a grand), BASS, VIBES, HORNS, STRGS, FLUTE,
                        SCRCH (tools/gen_builtin_hiphop.py makes them; FLUTE and the KIT:
                        tools/fetch_cc0.py), and the acoustic drum kit of the GM map (KIT)
  gen_waves.py          Felucca's own drum sounds (the Hügelton Sample Pack): a GM kit role the CC0 kit lacks
A file named ..._m<n>.wav has its root given (MIDI note n); else it comes from the note in the name.

The header is cached in build/gen_samples.cache under a hash of every input.
"""
import hashlib
import math
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sampleio as sio  # noqa: E402
from sampleio import detect_hz, hz_to_midi, ima_encode, key_split, onset, peak, read_any_wav, resample  # noqa: E402

SRC = Path(__file__).resolve().parents[1]
GENDIR = SRC / "build" / "genwav"
CACHE = SRC / "build" / "gen_samples.cache"
CC0 = SRC / "assets" / "samples-cc0"
TR = 22050                                   # stored sample rate

# set -> kind ("oneshot" decaying, "sus" looped sustain, "piano" its attack then a steady loop that the
# envelope fades, "kit" one sample per key).
# The other slots are user sets loaded from the web editor.
# fm1-drummer (drums only, docs/drums-only.md): the instrument sets (PIANO, BASS, VIBES, HORNS, STRGS, FLUTE,
# SCRCH: ~212 KB of flash) are no longer built; the KIT feeds the GM kit (PERC), the acoustic drum kit
CC0_SETS = [("KIT", "kit")]
MEASURED_TUNING = ()                         # sets recorded off A440 (measured near the named note)

KIT_BASE = 53                     # F3, the lowest FM-1 key

NOTE = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}

# GM drum map (General MIDI percussion, channel 10): role -> [(lo, hi, root)].
# One sample can serve several GM notes; a range is pitched around its root.
GM_KIT = {
    "kick": [(35, 36, 36)], "rim": [(37, 37, 37)], "snare": [(38, 38, 38), (40, 40, 40)],
    "clap": [(39, 39, 39)], "chh": [(42, 42, 42), (44, 44, 44)], "ohh": [(46, 46, 46)],
    # (the toms skip the hi-hats' 42, 44, 46: a zone over them would win, the hats would play a tom)
    "tomlo": [(41, 41, 43), (43, 43, 43), (45, 45, 43)], "tomhi": [(47, 48, 48), (50, 50, 48)],
    "tom": [(41, 41, 47), (43, 43, 47), (45, 45, 47), (47, 48, 47), (50, 50, 47)],
    "crash": [(49, 49, 49), (52, 52, 52), (55, 55, 55), (57, 57, 57)],
    "ride": [(51, 51, 51), (53, 53, 53), (59, 59, 59)],
    "tamb": [(54, 54, 54)], "cowbell": [(56, 56, 56)], "conga": [(62, 64, 63)],
    "shaker": [(69, 70, 70), (82, 82, 82)], "claves": [(75, 75, 75)], "wood": [(76, 77, 76)],
}
GM_ROLE_WORDS = [("bassdrum", "kick"), ("kick", "kick"), ("snare", "snare"), ("hihat", "chh"), ("chat", "chh"),
                 ("ohat", "ohh"), ("clap", "clap"), ("tom lo", "tomlo"), ("tom hi", "tomhi"), ("tomlo", "tomlo"),
                 ("tomhi", "tomhi"), ("tom", "tom"),
                 ("rim", "rim"), ("cowbell", "cowbell"), ("tamb", "tamb"), ("shaker", "shaker"),
                 ("conga", "conga"), ("claves", "claves"), ("wood", "wood"), ("crash", "crash"), ("ride", "ride")]
GM_KEEP = {"crash": 0.6, "ride": 0.6, "ohh": 0.6}   # (SLOOP 2.5: the cymbals 1.0 -> 0.6 s, 12.8 KB of flash back;
                                                      # the first 0.44 s as recorded, then a fade: GM_FADE)
GM_RATE = {"chh": 32000, "ohh": 32000, "crash": 32000, "ride": 32000}   # the metal keeps its top (else TR)
# an acoustic kit from the CC0 recordings (assets/samples-cc0/KIT: VCSL): the concert bass drum and the toms
# get a shorter body (a kit's tuned-up, damped heads): their level falls as exp(-t / tau) after a hold
GM_SHAPE = {"kick": (0.03, 0.13), "tomlo": (0.04, 0.22), "tomhi": (0.04, 0.2)}
GM_FADE = {"crash": 0.16, "ride": 0.16}               # the cymbals fade out over their last 0.16 s (else 30 ms)
# the balance of the kit (dB under full scale): the kick and the snare up front, the metal and the hand
# percussion further back, as the synthesised kits (tools/level_drumkits.py); the click's wood block stays
GM_GAIN = {"chh": -9, "ohh": -9, "crash": -8, "ride": -9, "shaker": -8, "tamb": -6, "conga": -4, "cowbell": -4,
           "claves": -4}

# SLICE's BREAK: (step, GM role, gain) on a bar of 16ths; the open hat is choked by the next hat
_wavs = {}


def wav(path):
    """read_any_wav, once per file and run (the CC0 pass reads every file twice)"""
    if path not in _wavs:
        _wavs[path] = read_any_wav(path)
    return _wavs[path]


def name_note(name):
    """'_C#3' in a CC0 file name -> (octave, pitch class)"""
    m = re.search(r"_([A-G]#?)(-?\d)", name)
    return (int(m.group(2)), NOTE[m.group(1)]) if m else None


def cc0_entries(setname, kind):
    """-> [(file name, int16 samples at TR, loop or None, root)]"""
    files = sorted((CC0 / setname).glob("*.wav"))
    out, conv = [], 1
    if kind != "kit" and not all(re.search(r"_m\d+\.wav$", p.name) for p in files):
        # octave convention of this set (C4 = 60 or C3 = 60): YIN votes, the names give the notes
        votes = {1: 0, 2: 0}
        for p in files:
            nn = name_note(p.name)
            if not nn:
                continue
            sr, x = wav(p)
            det = hz_to_midi(detect_hz(x[onset(x):], sr))
            for o in (1, 2):
                if abs(det - ((nn[0] + o) * 12 + nn[1])) < 1.0:
                    votes[o] += 1
        conv = 2 if votes[2] >= votes[1] else 1
    for k, p in enumerate(files):
        sr, x = wav(p)
        x = x[max(0, onset(x) - 16):]
        nn = name_note(p.name)
        given = re.search(r"_m(\d+)\.wav$", p.name)
        if given and kind != "kit":
            root = int(given.group(1))                  # (the file says it)
        elif kind == "kit":
            root = KIT_BASE + k
        elif nn:
            root = (nn[0] + conv) * 12 + nn[1]
            if setname in MEASURED_TUNING:              # tuned off A440: the measured pitch near the named note
                det = hz_to_midi(detect_hz(x, sr))
                if abs(det - root) < 1.0:
                    root = det
        else:
            root = hz_to_midi(detect_hz(x, sr))
        x = resample(x, sr, TR)
        keep = {"oneshot": 1.0, "sus": 0.95, "kit": 0.6, "piano": 1.0}[kind]
        x = x[:int(keep * TR)]
        n = len(x)
        if kind == "piano":                          # the attack, then a steady tone the envelope (DEC) fades:
            ls, le, xf, a0 = int(0.55 * TR), n - 1, int(0.08 * TR), int(0.30 * TR)   # long notes in 1 s
            w = int(0.02 * TR)
            env = [max(1e-6, (sum(v * v for v in x[max(0, i - w):i + w]) / (2 * w)) ** 0.5) for i in range(0, n, w)]
            ref = env[a0 // w]
            x = [v * (ref / env[min(len(env) - 1, i // w)] if i >= a0 else 1.0) for i, v in enumerate(x)]
            ls = best_loop_start(x, le, xf, int(0.45 * TR), int(0.65 * TR))   # (the tone in phase: no dip)
            for i in range(xf):
                a = i / xf
                x[le - xf + i] = x[le - xf + i] * (1 - a) + x[ls - xf + i] * a
            loop = (ls, le)
        elif kind == "sus":                          # crossfaded sustain loop in the steady part
            ls, le, xf = int(0.40 * TR), n - 1, int(0.06 * TR)
            for i in range(xf):
                a = i / xf
                x[le - xf + i] = x[le - xf + i] * (1 - a) + x[ls - xf + i] * a
            loop = (ls, le)
        else:
            fade = int(0.08 * TR)
            for i in range(fade):
                x[n - fade + i] *= 1 - i / fade
            loop = None
        pk = peak(x)
        out.append((p.name, [int(v * 30000 / pk) for v in x], loop, root))
    return out


def best_loop_start(x, le, xf, lo, hi):
    """the loop start in [lo, hi) whose last xf samples look most like the loop end's (correlation, on
    every 4th sample, every 2nd start: plain Python, no numpy): the crossfade then joins two copies of the
    same wave, not two phases that cancel"""
    tail = x[le - xf:le:4]
    tn = sum(v * v for v in tail) ** 0.5 + 1e-9
    best, bl = -2.0, (lo + hi) // 2
    for ls in range(lo, hi, 2):
        seg = x[ls - xf:ls:4]
        c = sum(a * b for a, b in zip(seg, tail)) / ((sum(v * v for v in seg) ** 0.5 + 1e-9) * tn)
        if c > best:
            best, bl = c, ls
    return bl


def gm_role(name):
    n = name.lower()
    return next((r for w, r in GM_ROLE_WORDS if w in n), None)


def gm_kit_sources(have_cc0):
    """role -> wav path: the CC0 acoustic kit (assets/samples-cc0/KIT); without it (and for SLICE's BREAK)
    Felucca's own synthesized drums (gen_waves.py) for the roles it lacks"""
    src = {}
    if have_cc0 and (CC0 / "KIT").exists():
        for p in sorted((CC0 / "KIT").glob("*.wav")):
            r = gm_role(p.name)
            if r and r not in src:
                src[r] = p
    for p in sorted(GENDIR.glob("D *.wav")):
        r = gm_role(p.stem)
        if r and r not in src:
            src[r] = p
    if "tomlo" in src or "tomhi" in src:
        src.pop("tom", None)
    return src


def gm_kit_entry(role, path, rate=TR):
    sr, x = wav(path)
    x = resample(x[max(0, onset(x) - 16):], sr, rate)
    x = x[:int(GM_KEEP.get(role, 0.45) * rate)]
    if role in GM_SHAPE:
        hold, tau = GM_SHAPE[role]
        h = int(hold * rate)
        x = [v * (1.0 if i < h else math.exp(-(i - h) / (tau * rate))) for i, v in enumerate(x)]
    pk = peak(x)
    end = len(x)                                    # drop the silent tail
    while end > 64 and abs(x[end - 1]) < 0.004 * pk:
        end -= 1
    x = x[:end]
    fade = min(len(x) // 4, int(GM_FADE.get(role, 0.03) * rate))
    for i in range(fade):
        x[len(x) - fade + i] *= 1 - i / fade
    g = 30000 / pk * 10 ** (GM_GAIN.get(role, 0) / 20) if path.parent.name == "KIT" else 30000 / pk
    return [int(v * g) for v in x]


class Builder:
    def __init__(self):
        self.zones, self.sets, self.blob, self.kinds = [], [], bytearray(), {}

    def add(self, s, loop_start):
        """ADPCM-encode s into the blob (each sample starts on an even offset) -> (offset, state at loop_start)"""
        enc, st = ima_encode(s, loop_start)
        off = len(self.blob)
        self.blob += enc
        if len(self.blob) & 1:
            self.blob.append(0)
        return off, st

    def add_set(self, name, kind, entries):
        """entries: zone dicts with root16 (and key for a kit); assigns the key ranges"""
        if not entries:
            return
        if kind == "kit":
            for e in entries:
                e["lo"] = e["hi"] = e["key"]
        else:
            entries.sort(key=lambda e: e["root16"])
            for e, (lo, hi) in zip(entries, key_split([e["root16"] // 16 for e in entries])):
                e["lo"], e["hi"] = lo, hi
        self.sets.append((name, len(self.zones), len(entries)))
        self.zones += entries
        self.kinds[name] = kind

    def gm_kit(self, have_cc0):
        """one GM-mapped kit: the CC0 acoustic kit"""
        z0 = len(self.zones)
        for role, path in gm_kit_sources(have_cc0).items():
            rate = GM_RATE.get(role, TR) if path.parent.name == "KIT" else TR
            smp = gm_kit_entry(role, path, rate)
            off, st = self.add(smp, len(smp))
            for lo, hi, root in GM_KIT[role]:
                self.zones.append(dict(off=off, n=len(smp), ls=len(smp), le=len(smp), looped=False, sr=rate,
                                       root16=root * 16, pred=st[0], idx=st[1], lo=lo, hi=hi))
        self.sets.append(("PERC", z0, len(self.zones) - z0))
        self.kinds["PERC"] = "kit"

    def cc0_set(self, name, kind):
        entries = []
        for k, (_, s, loop, root) in enumerate(cc0_entries(name, kind)):
            ls, le = loop if loop else (len(s), len(s))
            off, st = self.add(s, ls)
            entries.append(dict(off=off, n=len(s), ls=ls, le=le, looped=bool(loop), sr=TR,
                                root16=int(round(root * 16)), pred=st[0], idx=st[1],
                                key=KIT_BASE + k if kind == "kit" else None))
        self.add_set(name, kind, entries)

    def header(self):
        zones, sets, blob = self.zones, self.sets, self.blob
        L = ["/* generated by tools/gen_samples.py: IMA ADPCM sample sets */", "#pragma once",
             "#include <stdint.h>", ""]
        L.append(f"static const uint8_t SMP_DATA[{max(1, len(blob))}] = {{")
        for i in range(0, len(blob), 32):
            L.append("    " + ",".join(map(str, blob[i:i + 32])) + ",")
        if not blob:
            L.append("    0,")
        L.append("};")
        L.append("static const smp_zone_t SMP_ZONES[] = {")
        for e in zones:
            rate = int(round(e["sr"] / 44100 * 65536))
            L.append(f"    {{{e['off']}, {e['n']}, {e['ls']}, {e['le']}, {rate}, {e['root16']}, {e['pred']}, "
                     f"{e['idx']}, {e['lo']}, {e['hi']}, {1 if e['looped'] else 0}}},")
        if not zones:
            L.append("    {0, 0, 0, 0, 65536, 960, 0, 0, 0, 127, 0},")
        L.append("};")
        L.append("static const smp_set_t SMP_SETS[] = {")
        for name, z0, nz in sets:
            L.append(f'    {{"{name}", {z0}, {nz}}},')
        if not sets:
            L.append('    {"NONE", 0, 1},')
        L.append("};")
        L.append(f"#define SMP_NSETS {max(1, len(sets))}")
        names = ", ".join(f'"{n}"' for n, _, _ in sets or [("NONE", 0, 0)])
        L.append("#define SMP_SET_NAMES_INIT " + names)
        L.append("static const char *const SMP_SET_NAMES[] = {" + names + "};")
        return "\n".join(L) + "\n"

    def summary(self):
        return f"samples: {len(self.sets)} sets, {len(self.zones)} zones, {len(self.blob)} B ADPCM"


def input_key(have_cc0):
    """hash of everything the header depends on"""
    h = hashlib.sha256()
    here = Path(__file__).resolve().parent
    for p in (here / "gen_samples.py", here / "sampleio.py"):
        h.update(p.read_bytes())
    h.update(repr((sys.version_info[:2], have_cc0)).encode())   # sum() differs across versions
    files = sorted(GENDIR.glob("*.wav"))
    if have_cc0:
        files += sorted(CC0.glob("*/*.wav"))
    for p in files:
        h.update(str(p.relative_to(SRC)).encode() + b"\0")
        h.update(hashlib.sha256(p.read_bytes()).digest())
    return h.hexdigest()


def main(out):
    subprocess.run([sys.executable, str(Path(__file__).with_name("gen_waves.py")), str(GENDIR)], check=True)
    have_cc0 = CC0.exists() and any(CC0.glob("*/*.wav"))
    if not have_cc0:
        print("samples: no CC0 samples in assets/samples-cc0 - generated material only")
    key = input_key(have_cc0)
    try:
        ck, summary, text = CACHE.read_text().split("\n", 2)
        if ck == key:
            Path(out).write_text(text)
            print(summary + " (cached)")
            return
    except (OSError, ValueError):
        pass
    b = Builder()
    if have_cc0:
        for name, kind in CC0_SETS:
            if kind != "kit":                       # the CC0 KIT feeds the GM kit
                b.cc0_set(name, kind)
    b.gm_kit(have_cc0)
    text = b.header()
    Path(out).write_text(text)
    CACHE.parent.mkdir(parents=True, exist_ok=True)
    CACHE.write_text(f"{key}\n{b.summary()}\n{text}")
    print(b.summary())


if __name__ == "__main__":
    main(sys.argv[1])
