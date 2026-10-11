#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""fm1-arranger: a sampled drum kit for the FM-1 from a folder of WAV files, onto the user slots USR3+4.

    tools/mkkit.py NAME DIR                            # what it would make: lanes, lengths, the budget
    tools/mkkit.py NAME DIR --image build/host/kit.img # a flash image of the two slots, for tools/fm1_render.py --usrkit
    tools/mkkit.py NAME DIR --wavs OUT_DIR             # the 16 processed WAVs (KICK_C2.wav...), for the web editor's kit page
    tools/mkkit.py NAME DIR --build OUT_PREFIX         # OUT_PREFIX-usr3.hdr / .bin and -usr4: tools/fm1_sample_upload.py's form
    tools/mkkit.py NAME DIR --load                     # into USR3 and USR4 over USB (the FM-1 stopped)

The files are sorted onto the 16 lanes by their names (kick, bd; snare, sd; hat, hh, closed; open, ohh;
pedal; rim, side, stick, clave; clap; tom + lo / floor, tom + hi / rack; crash, china, splash; ride;
shaker, tamb; conga, bongo; cowbell, bell; kick2 / snare2 for the second ones). The first file for a
lane wins; --map LANE=file overrides. Each sound: mono, 22050 Hz, the silence before the hit cut, the
tail cut at the lane's length (a fade of 20 ms) and shortened further if the slot runs out (a slot holds
about 7.4 s of IMA ADPCM: lanes KICK..RIM in USR3, SNARE2..COWBELL in USR4, as the device looks them up),
its peak set by the lane's gain (kick 0 dB, hats -9, crash -6...; --gain LANE=dB adjusts). The device
plays the kit as KIT "USR3+4" (the index after the built-in and SYN kits: `fm1_render.py --kit` with
--usrkit picks it by itself). CC0 kits: assets/samples-cc0/KIT is one, VSCO-2 CE and VCSL have more."""
import argparse
import math
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sampleio as sio  # noqa: E402

LANES = ["KICK", "KICK2", "SNARE", "CLAP", "HAT", "OPENHAT", "PEDAL", "RIM",
         "SNARE2", "LOWTOM", "HITOM", "CRASH", "RIDE", "SHAKER", "CONGA", "COWBELL"]
LANE_NOTE = dict(KICK=36, KICK2=35, SNARE=38, CLAP=39, HAT=42, OPENHAT=46, PEDAL=44, RIM=37,
                 SNARE2=40, LOWTOM=43, HITOM=48, CRASH=49, RIDE=51, SHAKER=70, CONGA=63, COWBELL=56)
WORDS = [                                         # (lane, its words, second-choice words); the word nearest the
    ("OPENHAT", ("openhat", "open", "ohat", "ohh"), ("oh_", "_oh", "hho")),   # start of the file name names the lane
    ("PEDAL", ("pedal", "phat"), ("_ph", "footh", "hhp")),
    ("HAT", ("hihat", "hi-hat", "hat", "hh", "chh"), ("closed",)),
    ("KICK2", ("kick2", "bd2", "kick_2", "bd_2"), ()),
    ("KICK", ("kick", "bdrum", "bd"), ("bass",)),
    ("SNARE2", ("snare2", "sd2", "snare_2", "sd_2"), ()),
    ("SNARE", ("snare", "sd", "sn_", "_sn"), ()),
    ("RIM", ("rim", "side", "stick"), ("clave", "wood", "click")),
    ("CLAP", ("clap",), ()),
    ("LOWTOM", ("tomlo", "lowtom", "floor", "tom_l", "toml", "ftom", "tom1"), ("low",)),
    ("HITOM", ("tomhi", "hitom", "rack", "tom_h", "tomh", "tom2"), ("high", "tom")),
    ("CRASH", ("crash", "china", "splash"), ("cymb",)),
    ("RIDE", ("ride",), ()),
    ("SHAKER", ("shaker", "tamb", "maraca", "cabasa"), ("egg",)),
    ("CONGA", ("conga", "bongo"), ("djembe",)),
    ("COWBELL", ("cowbell", "agogo"), ("bell",)),
]
MAX_S = dict(KICK=0.7, KICK2=0.7, SNARE=0.9, CLAP=0.5, HAT=0.45, OPENHAT=1.2, PEDAL=0.35, RIM=0.4,
             SNARE2=0.9, LOWTOM=1.0, HITOM=0.9, CRASH=1.9, RIDE=1.5, SHAKER=0.4, CONGA=0.5, COWBELL=0.4)
GAIN_DB = dict(KICK=0, KICK2=0, SNARE=-1, CLAP=-3, HAT=-9, OPENHAT=-8, PEDAL=-10, RIM=-6,
               SNARE2=-2, LOWTOM=-2, HITOM=-2.5, CRASH=-6, RIDE=-8, SHAKER=-12, CONGA=-4, COWBELL=-6)
SLOTS = {2: LANES[:8], 3: LANES[8:]}              # USR3: KICK..RIM, USR4: SNARE2..COWBELL (drums.c: USR3 first, else USR4)
FADE = 0.02
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def note_name(n):
    return f"{NOTE_NAMES[n % 12]}{n // 12 - 1}"


def lane_of(name):
    """(lane, rank) for a file name: the lane whose word comes first in it (rank 0 a first-choice word,
    1 a second-choice one); None for no word at all"""
    key = re.sub(r"[^a-z0-9_]", "_", name.lower())
    best = None
    for lane, words, second in WORDS:
        for rank, ws in ((0, words), (1, second)):
            for w in ws:
                i = key.find(w)
                if i >= 0 and (best is None or (i, rank) < best[:2]):
                    best = (i, rank, lane)
    return (best[2], best[1]) if best else None


def process(path, lane, gain_db, max_s):
    """mono float at 22050 Hz: the hit from its onset, the tail cut at max_s with a fade, peak at the gain"""
    from pathlib import Path
    sr, x = sio.read_any_wav(Path(path))
    x = sio.resample(x, sr, sio.SLOT_RATE)
    if not x:
        raise ValueError(f"{path}: no audio")
    pk = sio.peak(x)
    start = max(0, sio.onset(x, 0.02) - int(0.001 * sio.SLOT_RATE))
    end = len(x)
    while end > start + 1 and abs(x[end - 1]) < pk * 0.002:    # the tail below -54 dB
        end -= 1
    x = x[start:end]
    return cut(x, max_s), pk


def cut(x, max_s):
    n = int(max_s * sio.SLOT_RATE)
    if len(x) > n:
        x = x[:n]
        f = int(FADE * sio.SLOT_RATE)
        for i in range(f):
            x[n - f + i] *= 1.0 - i / f
    return x


def to_int16_gain(x, gain_db):
    pk = sio.peak(x)
    g = 30000 * 10 ** (gain_db / 20) / pk
    return [max(-32768, min(32767, int(v * g))) for v in x]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("name", help="the kit's name (8 characters)")
    ap.add_argument("dir", help="a folder of WAV files")
    ap.add_argument("--map", action="append", default=[], metavar="LANE=FILE", help="a lane's file, by hand")
    ap.add_argument("--gain", action="append", default=[], metavar="LANE=DB", help="a lane's peak, dB (0 = full)")
    ap.add_argument("--image", help="write a flash image of USR3 and USR4 (2 x 80 KiB) for fm1_render.py --usrkit")
    ap.add_argument("--wavs", help="write the processed WAVs here (for the web editor's kit page)")
    ap.add_argument("--build", help="write PREFIX-usr3.hdr/.bin and PREFIX-usr4.hdr/.bin")
    ap.add_argument("--load", action="store_true", help="send the kit into USR3 and USR4 over USB")
    a = ap.parse_args()
    files, ranks = {}, {}
    for fn in sorted(os.listdir(a.dir)):
        if not fn.lower().endswith(".wav"):
            continue
        hit = lane_of(os.path.splitext(fn)[0])
        if not hit:
            print(f"  (no lane for {fn})")
            continue
        lane, rank = hit
        if lane not in files or rank < ranks[lane]:   # the first file for a lane wins, a first-choice word over a second
            files[lane], ranks[lane] = os.path.join(a.dir, fn), rank
    for m in a.map:
        lane, _, f = m.partition("=")
        if lane.upper() not in LANES:
            sys.exit(f"--map: no lane {lane}; lanes: {' '.join(LANES)}")
        files[lane.upper()] = f if os.path.isabs(f) or os.path.exists(f) else os.path.join(a.dir, f)
    gains = dict(GAIN_DB)
    for g in a.gain:
        lane, _, db = g.partition("=")
        gains[lane.upper()] = float(db)
    if not files:
        sys.exit("no WAV files named for any lane")
    sounds = {}
    for lane, path in files.items():
        x, pk = process(path, lane, gains[lane], MAX_S[lane])
        sounds[lane] = x
    budget = sio.SLOT_MAX_DATA * 2                    # samples a slot holds (2 ADPCM nibbles a byte)
    slots = {}
    print(f"{a.name}: {len(sounds)} lanes from {a.dir}")
    for k, lanes in SLOTS.items():
        have = [l for l in lanes if l in sounds]
        total = sum(len(sounds[l]) for l in have)
        while total > budget and have:                # too long: the longest lanes give way
            longest = max(have, key=lambda l: len(sounds[l]))
            sounds[longest] = cut(sounds[longest], (len(sounds[longest]) - (total - budget) - 1) / sio.SLOT_RATE)
            total = sum(len(sounds[l]) for l in have)
        zones = [(to_int16_gain(sounds[l], gains[l]), LANE_NOTE[l], LANE_NOTE[l], LANE_NOTE[l]) for l in have]
        slots[k] = sio.user_slot(a.name, zones) if zones else None
        print(f"  USR{k + 1}: {total / sio.SLOT_RATE:5.2f} s of {budget / sio.SLOT_RATE:.2f}")
        for l in have:
            print(f"    {l:8s} {len(sounds[l]) / sio.SLOT_RATE:5.2f} s {gains[l]:+5.1f} dB  {os.path.basename(files[l])}")
    missing = [l for l in LANES if l not in sounds]
    if missing:
        print("  no sound for: " + ", ".join(missing))
    if a.wavs:
        os.makedirs(a.wavs, exist_ok=True)
        import wave
        for l, x in sounds.items():
            p = os.path.join(a.wavs, f"{l}_{note_name(LANE_NOTE[l])}.wav")
            with wave.open(p, "wb") as w:
                w.setnchannels(1); w.setsampwidth(2); w.setframerate(sio.SLOT_RATE)
                w.writeframes(struct.pack(f"<{len(x)}h", *to_int16_gain(x, gains[l])))
        print(f"wrote {len(sounds)} WAVs into {a.wavs}")
    if a.build:
        for k, s in slots.items():
            if s:
                open(f"{a.build}-usr{k + 1}.hdr", "wb").write(s[0])
                open(f"{a.build}-usr{k + 1}.bin", "wb").write(s[1])
        print(f"wrote {a.build}-usr3/4 .hdr / .bin")
    if a.image:
        img = bytearray(b"\xFF" * (2 * sio.SLOT_SIZE))
        for i, k in enumerate((2, 3)):
            if slots[k]:
                hdr, data = slots[k]
                img[i * sio.SLOT_SIZE:i * sio.SLOT_SIZE + len(hdr)] = hdr
                img[i * sio.SLOT_SIZE + len(hdr):i * sio.SLOT_SIZE + sio.SLOT_DATA_OFF] = bytes(sio.SLOT_DATA_OFF - len(hdr))
                img[i * sio.SLOT_SIZE + sio.SLOT_DATA_OFF:i * sio.SLOT_SIZE + sio.SLOT_DATA_OFF + len(data)] = data
        open(a.image, "wb").write(img)
        print(f"wrote {a.image}: USR3 and USR4 as the device holds them")
    if a.load:
        import fm1_sample_upload as up
        link = up.Link()
        for k, s in slots.items():
            if not s:
                continue
            hdr, data = s
            if link.req(11, [k])[1]:
                sys.exit(f"USR{k + 1}: begin failed")
            for off in range(0, len(data), 256):
                at = sio.SLOT_DATA_OFF + off
                r = link.req(12, [k, at & 0x7F, (at >> 7) & 0x7F, (at >> 14) & 0x7F] + up.pack7(data[off:off + 256]))
                if r[4]:
                    sys.exit(f"USR{k + 1}: write at {at:#x} failed ({r[4]})")
            r = link.req(13, [k] + up.pack7(hdr), 5)
            print(f"USR{k + 1}:", "ok" if r[1] == 0 else f"FAILED ({r[1]})")


if __name__ == "__main__":
    main()
