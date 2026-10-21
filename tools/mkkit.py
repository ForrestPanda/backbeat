#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""fm1-drummer: a sampled drum kit for the FM-1 from a folder of WAV files, onto the user slots USR3+4.

    tools/mkkit.py NAME DIR                            # what it would make: lanes, lengths, the budget
    tools/mkkit.py NAME DIR --image build/host/kit.img # a flash image of the two slots, for tools/fm1_render.py --usrkit
    tools/mkkit.py NAME DIR --wavs OUT_DIR             # the 16 processed WAVs (KICK_C2.wav...), for the web editor's kit page
    tools/mkkit.py NAME DIR --build OUT_PREFIX         # OUT_PREFIX-usr3.hdr / .bin and -usr4: tools/fm1_sample_upload.py's form
    tools/mkkit.py NAME DIR --load                     # into USR3 and USR4 over USB (the FM-1 stopped)
    tools/mkkit.py NAME --sfz Red_Zeppelin_5pc.sfz ... # from an SFZ kit (velocity layers): the hardest layer of
                                                       # each GM key onto its lane, FLAC decoded with sox

The files are sorted onto the 16 lanes by their names (kick, bd; snare, sd; hat, hh, closed; open, ohh;
pedal; rim, side, stick, clave; clap; tom + lo / floor, tom + hi / rack; crash, china, splash; ride;
shaker, tamb; tom + mid (the mid tom, where the conga was); cowbell, bell; kick2 / snare2 for the second ones). The first file for a
lane wins; --map LANE=file overrides. Each sound: mono, 22050 Hz, the silence before the hit cut, the
tail cut at the lane's length (a fade of 20 ms) and shortened further if the slot runs out (a slot holds
about 7.4 s of IMA ADPCM: lanes KICK..RIM in USR3, SNARE2..COWBELL in USR4, as the device looks them up),
its peak set by the lane's gain (kick 0 dB, hats -9, crash -6...; --gain LANE=dB adjusts). --layers: two
velocity layers, USR3 the hard hits of the drums and hats and USR4 their softer ones with the cymbals and the
extras (the device picks by the hit's level: ghost and soft hits from USR4, and a lane it finds in one slot only); --hires HAT,PEDAL,RIM,CLAP keeps the bright short sounds at
44.1 kHz (twice the flash a second). In a folder, a lane's soft hit is LANE_note-soft.wav. The device
plays the kit as KIT "USR3+4" (the index after the built-in and SYN kits: `fm1_render.py --kit` with
--usrkit picks it by itself). CC0 kits: assets/samples-cc0/KIT is one, VSCO-2 CE and VCSL have more."""
import argparse
import math
import os
import re
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sampleio as sio  # noqa: E402

LANES = ["KICK", "KICK2", "SNARE", "CLAP", "HAT", "OPENHAT", "PEDAL", "RIM",
         "SNARE2", "LOWTOM", "MIDTOM", "HITOM", "CRASH", "RIDE", "SHAKER", "COWBELL"]
LANE_NOTE = dict(KICK=36, KICK2=35, SNARE=38, CLAP=39, HAT=42, OPENHAT=46, PEDAL=44, RIM=37,
                 SNARE2=40, LOWTOM=43, HITOM=48, CRASH=49, RIDE=51, SHAKER=70, MIDTOM=45, COWBELL=56)
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
    ("MIDTOM", ("midtom", "tommid", "tom_m", "tomm", "mtom", "tom3"), ("mid",)),   # (before HITOM: "tom" is its fallback)
    ("HITOM", ("tomhi", "hitom", "rack", "tom_h", "tomh", "tom2"), ("high", "tom")),
    ("CRASH", ("crash", "china", "splash"), ("cymb",)),
    ("RIDE", ("ride",), ()),
    ("SHAKER", ("shaker", "tamb", "maraca", "cabasa"), ("egg",)),
    ("COWBELL", ("cowbell", "agogo"), ("bell",)),
]
MAX_S = dict(KICK=0.7, KICK2=0.7, SNARE=0.9, CLAP=0.5, HAT=0.45, OPENHAT=1.2, PEDAL=0.35, RIM=0.4,
             SNARE2=0.9, LOWTOM=1.0, HITOM=0.9, CRASH=1.9, RIDE=1.5, SHAKER=0.4, MIDTOM=0.95, COWBELL=0.4)
GAIN_DB = dict(KICK=0, KICK2=0, SNARE=-1, CLAP=-3, HAT=-9, OPENHAT=-8, PEDAL=-10, RIM=-6,
               SNARE2=-2, LOWTOM=-2, HITOM=-2.5, CRASH=-6, RIDE=-8, SHAKER=-12, MIDTOM=-2.25, COWBELL=-6)
MIN_S = dict(CRASH=1.2, RIDE=1.2, OPENHAT=0.5, LOWTOM=0.5, HITOM=0.5, MIDTOM=0.5)   # when a slot overflows: these keep this much
MIN_DEFAULT = 0.25
SOFT_MAX = dict(KICK=0.45, KICK2=0.45, SNARE=0.45, SNARE2=0.45, OPENHAT=0.5, LOWTOM=0.5, HITOM=0.5)   # soft hits: shorter
EXTRA_MAX = dict(KICK2=0.45, CLAP=0.3, SHAKER=0.3, COWBELL=0.3)   # the rare lanes, in the second slot
SLOTS = {2: LANES[:8], 3: LANES[8:]}              # USR3: KICK..RIM, USR4: SNARE2..COWBELL (drums.c: USR3 first, else USR4)
# an SFZ kit: the GM keys each lane takes, in order of preference ("36:3" = key 36, its 3rd layer from the top:
# KICK2 and SNARE2 as softer hits of the same drum when the kit has no second one)
SFZ_LANES = {"KICK": ["36", "35"], "KICK2": ["35", "36:3"], "SNARE": ["38"], "SNARE2": ["40", "38:3"], "RIM": ["37"],
             "CLAP": ["39"], "HAT": ["42"], "OPENHAT": ["46", "48"], "PEDAL": ["44"], "LOWTOM": ["43", "41", "45", "63"],
             "HITOM": ["48", "47", "50", "45", "61"], "CRASH": ["49", "57", "55", "52"], "RIDE": ["51", "59", "53"],
             "SHAKER": ["70", "54", "69", "82", "64"], "MIDTOM": ["45", "47", "43", "61"], "COWBELL": ["56", "67", "68"]}
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


def process(path, lane, gain_db, max_s, hires=False):
    """(mono float, rate): the hit from its onset, the tail cut at max_s with a fade; at 22050 Hz, or at 44100 Hz
    when hires (the bright short sounds keep their top end; the source resampled to 44100 if it is not there)"""
    from pathlib import Path
    sr, x = sio.read_any_wav(Path(path))
    rate = 44100 if hires else sio.SLOT_RATE
    x = sio.resample(x, sr, rate)
    if not x:
        raise ValueError(f"{path}: no audio")
    pk = sio.peak(x)
    start = max(0, sio.onset(x, 0.02) - int(0.001 * rate))
    end = len(x)
    while end > start + 1 and abs(x[end - 1]) < pk * 0.002:    # the tail below -54 dB
        end -= 1
    x = x[start:end]
    return cut(x, max_s, rate), rate


def cut(x, max_s, rate=sio.SLOT_RATE):
    n = max(int(max_s * rate), int(FADE * rate) + 1)   # (never shorter than the fade)
    if len(x) > n:
        x = x[:n]
        f = int(FADE * rate)
        for i in range(f):
            x[n - f + i] *= 1.0 - i / f
    return x


def to_int16_gain(x, gain_db):
    pk = sio.peak(x)
    g = 30000 * 10 ** (gain_db / 20) / pk
    return [max(-32768, min(32767, int(v * g))) for v in x]


def sfz_regions(path):
    """{key: [region dict]} of an SFZ file: every region with its opcodes, those of its group, master, global
    and control (default_path, #include) folded in; key from key / pitch_keycenter / lokey; sample paths absolute"""
    base = os.path.dirname(os.path.abspath(path))
    text = open(path, encoding="utf-8", errors="replace").read()
    text = re.sub(r"//[^\n]*", "", text)
    for _ in range(4):                                # includes, nested (ARIA kits: keymap, macros, controls)
        incs = re.findall(r'#include\s+"([^"]+)"', text)
        if not incs:
            break
        for inc in incs:
            try:
                body = re.sub(r"//[^\n]*", "", open(os.path.join(base, inc), encoding="utf-8", errors="replace").read())
            except OSError:
                body = ""
            text = text.replace(f'#include "{inc}"', "\n" + body + "\n")   # (on its own lines: a define inside)
    macros = {}                                       # ARIA macros ($name value), redefined along the file: in order
    out = []
    for line in text.split("\n"):
        m = re.match(r"\s*#define\s+(\$\w+)\s+(.*)$", line)
        if m:
            macros[m.group(1)] = re.sub(r"\$\w+", lambda t: macros.get(t.group(0), t.group(0)), m.group(2).strip())
            continue
        out.append(re.sub(r"\$\w+", lambda t: macros.get(t.group(0), t.group(0)), line) if "$" in line else line)
    text = "\n".join(out)
    ctx = {"control": {}, "global": {}, "master": {}, "group": {}}
    regions = {}
    for tag, body in re.findall(r"<(\w+)>([^<]*)", text):
        kv = dict(re.findall(r'(\w+)=("[^"]*"|\S+)', body))
        kv = {k: v.strip('"') for k, v in kv.items()}
        if "sample" in kv:                        # (a sample name with spaces: take the rest of the line)
            m = re.search(r"sample=(.+?)(?=\s+\w+=|$)", body.strip().replace("\n", " "))
            if m:
                kv["sample"] = m.group(1).strip().strip('"')
        if tag in ("control", "global", "master", "group"):
            ctx[tag] = kv
            if tag == "global":
                ctx["master"] = {}
                ctx["group"] = {}
            elif tag == "master":
                ctx["group"] = {}
        elif tag == "region":
            r = {**ctx["global"], **ctx["master"], **ctx["group"], **kv}
            if "sample" not in r:
                continue
            key = r.get("key") or r.get("pitch_keycenter") or r.get("lokey")
            if key is None:
                continue
            r["_path"] = os.path.normpath(os.path.join(base, ctx["control"].get("default_path", ""), r["sample"].replace("\\", "/")))
            regions.setdefault(str(int(float(key))), []).append(r)
    return regions


def sfz_layer(regions, want):
    """the regions of one hit: "key" or "key:n" (the n-th velocity layer from the top, 1 = the hardest), the first
    round robin, every microphone of it (regions sharing the layer and the round robin)"""
    key, _, layer = want.partition(":")
    lst = regions.get(key)
    if not lst:
        return []
    layers = sorted({int(r.get("lovel", 1)) for r in lst}, reverse=True)
    n = int(layer) if layer else 1
    lovel = layers[min(n, len(layers)) - 1]
    cands = [r for r in lst if int(r.get("lovel", 1)) == lovel]
    rr = lambda r: (float(r.get("lorand", 0)), int(r.get("seq_position", 1)))
    first = min(rr(r) for r in cands)
    return [r for r in cands if rr(r) == first]


def mic_gain(path, nregs):
    """a multi-mic hit (DrumGizmo ports: '<n>-<Instrument>-<Mic>.flac'): the instrument's own mics full, the overheads
    -3 dB, the ambience -6 dB, the other drums' mics (bleed) -14 dB, a trigger track left out; None = leave out"""
    m = re.match(r"\d+-([^-]+)-([A-Za-z0-9]+)\.\w+$", os.path.basename(path))   # (the instrument may hold an
                                                     # underscore, DRSKit's "Snare_rim": all its mics were full, 2026-10-21)
    if not m or nregs < 4:
        return 0.0
    instr, mic = m.group(1).lower(), m.group(2).lower()
    if "trigger" in mic:
        return None
    if mic.startswith(instr[:4]) or instr.startswith(mic[:4]):
        return 0.0
    if mic.startswith("oh") or "overhead" in mic:
        return -3.0
    if mic.startswith("amb") or "room" in mic:
        return -6.0
    return -14.0


def sfz_mix(regs, dst):
    """the regions' samples decoded (sox), summed with their volume (dB) and the mic's place (mic_gain) into one mono
    16-bit WAV at the source rate"""
    import wave
    tracks = []
    rate = None
    for r in regs:
        if not os.path.exists(r["_path"]):
            continue
        mg = mic_gain(r["_path"], len(regs))
        if mg is None:
            continue
        tmp = dst + f".{len(tracks)}.wav"
        if subprocess.run(["sox", r["_path"], "-b", "16", "-c", "1", tmp], capture_output=True).returncode:
            continue
        with wave.open(tmp, "rb") as w:
            if rate is None:
                rate = w.getframerate()
            if w.getframerate() != rate:
                os.remove(tmp)
                continue
            x = struct.unpack(f"<{w.getnframes()}h", w.readframes(w.getnframes()))
        os.remove(tmp)
        g = 10 ** ((float(r.get("volume", 0)) + mg) / 20)
        tracks.append([v * g for v in x])
    if not tracks:
        return False
    n = max(len(t) for t in tracks)
    mix = [0.0] * n
    for t in tracks:
        for i, v in enumerate(t):
            mix[i] += v
    pk = max(1.0, max(abs(v) for v in mix))
    scale = min(1.0, 32000 / pk)
    with wave.open(dst, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
        w.writeframes(struct.pack(f"<{n}h", *(int(v * scale) for v in mix)))
    return True


def sfz_files(path, tmpdir, overrides=None, layer=None, lanes=None):
    """{lane: wav path} from an SFZ kit: the chosen hit of each lane mixed down into tmpdir; layer: every lane's
    hit from that velocity layer (1 = the hardest) instead of the lane's own choice; lanes: only these"""
    regions = sfz_regions(path)
    files = {}
    for lane, wants in SFZ_LANES.items():
        if lanes and lane not in lanes:
            continue
        if overrides and lane in overrides:
            wants = [overrides[lane]]
        if layer:
            wants = [w.split(":")[0] + f":{layer}" for w in wants]
        for w in wants:
            regs = sfz_layer(regions, w)
            if regs:
                dst = os.path.join(tmpdir, f"{lane}{'-soft' if layer else ''}.wav")
                if sfz_mix(regs, dst):
                    files[lane] = dst
                    break
    return files


def sfz_report(path):
    """what an SFZ kit holds: per key, the layers, the round robins, the samples (to map an unusual kit by hand)"""
    regions = sfz_regions(path)
    print(f"{path}: {sum(len(v) for v in regions.values())} regions on {len(regions)} keys")
    for key in sorted(regions, key=int):
        lst = regions[key]
        layers = sorted({int(r.get("lovel", 1)) for r in lst})
        rrs = {(float(r.get("lorand", 0)), int(r.get("seq_position", 1))) for r in lst}
        names = sorted({os.path.basename(r["_path"]) for r in lst})
        print(f"  key {key:>3}: {len(layers)} layers, {len(rrs)} round robins, {len(lst)} regions: {names[0]}" + (f" .. {names[-1]}" if len(names) > 1 else ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("name", help="the kit's name (8 characters)")
    ap.add_argument("dir", nargs="?", help="a folder of WAV files (or --sfz)")
    ap.add_argument("--sfz", help="an SFZ kit: the hardest layer of each GM key onto its lane (sox decodes FLAC)")
    ap.add_argument("--sfz-keys", action="store_true", help="only list what the SFZ kit holds, key by key")
    ap.add_argument("--map", action="append", default=[], metavar="LANE=FILE", help="a lane's file, by hand")
    ap.add_argument("--gain", action="append", default=[], metavar="LANE=DB", help="a lane's peak, dB (0 = full)")
    ap.add_argument("--image", help="write a flash image of USR3 and USR4 (2 x 80 KiB) for fm1_render.py --usrkit")
    ap.add_argument("--wavs", help="write the processed WAVs here (for the web editor's kit page)")
    ap.add_argument("--build", help="write PREFIX-usr3.hdr/.bin and PREFIX-usr4.hdr/.bin")
    ap.add_argument("--load", action="store_true", help="send the kit into USR3 and USR4 over USB")
    ap.add_argument("--layers", action="store_true", help="two velocity layers: USR3 the hard hits of every lane, USR4 the soft ones of the drums and hats (the device picks by the hit's level)")
    ap.add_argument("--soft-layer", type=int, default=3, help="with --layers and --sfz: the velocity layer of the soft hits, 1 = the hardest (default 3)")
    ap.add_argument("--soft-lanes", default="KICK,SNARE,SNARE2,HAT,OPENHAT,PEDAL", help="with --layers: the lanes that get a soft hit (add LOWTOM,HITOM if the slot has room)")
    ap.add_argument("--hires", default="", help="lanes kept at 44100 Hz (twice the flash a second): e.g. HAT,PEDAL,RIM,CLAP")
    a = ap.parse_args()
    files, ranks = {}, {}
    tmp = tempfile.mkdtemp(prefix="mkkit-")
    if a.sfz and a.sfz_keys:
        sfz_report(a.sfz)
        return
    if a.sfz:
        over = {}
        for m in a.map:                               # --map LANE=KEY[:layer] picks a key of the SFZ kit
            lane, _, v = m.partition("=")
            over[lane.upper()] = v
        files = sfz_files(a.sfz, tmp, over)
        soft_files = sfz_files(a.sfz, tmp, over, layer=a.soft_layer, lanes=set(a.soft_lanes.upper().split(","))) if a.layers else {}
        a.map = []
        a.dir = a.dir or os.path.dirname(a.sfz)
    elif not a.dir:
        sys.exit("a folder of WAV files, or --sfz")
    if not a.sfz:
        soft_files = {}
    for fn in sorted(os.listdir(a.dir)) if not a.sfz else []:
        if not fn.lower().endswith(".wav"):
            continue
        stem = os.path.splitext(fn)[0]
        soft = stem.lower().endswith("-soft")
        hit = lane_of(stem[:-5] if soft else stem)
        if not hit:
            print(f"  (no lane for {fn})")
            continue
        lane, rank = hit
        if soft:
            if a.layers and lane not in soft_files and lane in set(a.soft_lanes.upper().split(",")):
                soft_files[lane] = os.path.join(a.dir, fn)
            continue
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
    hires = {x.strip().upper() for x in a.hires.split(",") if x.strip()}
    for h in hires:
        if h not in LANES:
            sys.exit(f"--hires: no lane {h}")
    sounds, soft = {}, {}                             # lane -> (samples, rate)
    for lane, path in files.items():
        sounds[lane] = process(path, lane, gains[lane], MAX_S[lane], lane in hires)
    for lane, path in soft_files.items():
        soft[lane] = process(path, lane, gains[lane], SOFT_MAX.get(lane, MAX_S[lane]), lane in hires)
    budget = sio.SLOT_MAX_DATA * 2 - 2 * 16           # samples a slot holds (2 ADPCM nibbles a byte; a byte spare a zone)
    # the layouts: SLOOP's split (KICK..RIM in USR3, the rest in USR4), or two layers: USR3 the hard hits of the
    # drums, hats and cymbals, USR4 their soft hits plus the hard hits of the extras (the device looks in the
    # other slot for a lane it does not find)
    EXTRAS = ["KICK2", "CLAP", "SHAKER", "COWBELL", "CRASH", "RIDE"]   # (the cymbals: long, one layer)
    main_lanes = [l for l in LANES if l not in EXTRAS]
    layout = {2: ("hard", main_lanes), 3: ("soft", LANES)} if a.layers else {2: ("hard", LANES[:8]), 3: ("hard", LANES[8:])}
    if a.layers:
        for l in EXTRAS:
            if l in sounds and l not in soft:
                x, rate = sounds[l]                   # (the extras' hard hits live in USR4, the rare ones shorter)
                soft[l] = (cut(x, EXTRA_MAX.get(l, MAX_S[l]), rate), rate)
                soft_files[l] = files[l]
    slots = {}
    print(f"{a.name}: {len(sounds)} lanes from {a.sfz or a.dir}" + (f", {len(soft)} soft" if a.layers else "") +
          (f", 44.1 kHz: {' '.join(sorted(hires))}" if hires else ""))
    for k, (which, lanes) in layout.items():
        pool = sounds if which == "hard" else soft
        have = [l for l in lanes if l in pool]
        total = sum(len(pool[l][0]) for l in have)
        while total > budget and have:                # too long: the longest sound above its floor loses a tenth
            above = [l for l in have if len(pool[l][0]) / pool[l][1] > MIN_S.get(l, MIN_DEFAULT) * 1.01]
            longest = max(above or have, key=lambda l: len(pool[l][0]) / pool[l][1])
            x, rate = pool[longest]
            want = max(0.05, MIN_S.get(longest, MIN_DEFAULT) if not above else max(MIN_S.get(longest, MIN_DEFAULT), 0.9 * len(x) / rate))
            pool[longest] = (cut(x, want, rate), rate)
            if sum(len(pool[l][0]) for l in have) >= total:
                break                                 # (nothing left to cut)
            total = sum(len(pool[l][0]) for l in have)
        zones = [(to_int16_gain(pool[l][0], gains[l] - (3 if which == "soft" else 0)), LANE_NOTE[l], LANE_NOTE[l], LANE_NOTE[l], pool[l][1]) for l in have]
        slots[k] = sio.user_slot(a.name, zones) if zones else None
        print(f"  USR{k + 1} ({which}): {total / sio.SLOT_RATE:5.2f} s of {budget / sio.SLOT_RATE:.2f} (in 22 kHz seconds)")
        for l in have:
            x, rate = pool[l]
            print(f"    {l:8s} {len(x) / rate:5.2f} s {gains[l]:+5.1f} dB{' 44.1k' if rate == 44100 else ''}  {os.path.basename((files if which == 'hard' else soft_files)[l])}")
    missing = [l for l in LANES if l not in sounds]
    if missing:
        print("  no sound for: " + ", ".join(missing))
    if a.wavs:
        os.makedirs(a.wavs, exist_ok=True)
        import wave
        for which, pool in (("", sounds), ("-soft", soft)):
            for l, (x, rate) in pool.items():
                p = os.path.join(a.wavs, f"{l}_{note_name(LANE_NOTE[l])}{which}.wav")
                with wave.open(p, "wb") as w:
                    w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
                    w.writeframes(struct.pack(f"<{len(x)}h", *to_int16_gain(x, gains[l] - (3 if which else 0))))
        print(f"wrote {len(sounds) + len(soft)} WAVs into {a.wavs}")
    if a.build:
        for k, sl in slots.items():
            if sl:
                open(f"{a.build}-usr{k + 1}.hdr", "wb").write(sl[0])
                open(f"{a.build}-usr{k + 1}.bin", "wb").write(sl[1])
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
        for k, sl in slots.items():
            if not sl:
                continue
            hdr, data = sl
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
