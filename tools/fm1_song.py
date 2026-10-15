#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""fm1-drummer: songs for the FM-1 from a JSON file (firmware/src/sng.c).

    tools/fm1_song.py song.json                     check it, print its sections and size
    tools/fm1_song.py song.json --out song.bin      the packed song (as the device stores it)
    tools/fm1_song.py song.json --put 0             pack and send it into song slot 0 (the FM-1 stopped, by USB)
    tools/fm1_song.py --get 0 --out song.bin        read slot 0 back (packed)
    tools/fm1_song.py --erase 0                     erase slot 0
    tools/fm1_song.py --pedal-channel 16            the pedal channel: 0 off, 1..16, 17 any (kept through power-off)
    tools/fm1_song.py --count-in 1                  bars of clicks before a section started from stopped: 0, 1, 2

The JSON (see docs/design.md):

    {"name": "MY SONG", "bpm": 120, "kit": 46, "swing": 0, "drum_level": 100, "drum_reverb": 20,
     "sections": [
       {"name": "VERSE", "steps": 16, "div": "1/16", "time": "4/4", "outro": false,
        "lanes": {"KICK": "x...x...x...x...", "SNARE": "....x.......x...", "HAT": "x.x.x.x.x.x.x.x."},
        "ratchets": {"SNARE": "............2..."},
        "fills": {"HITOM": "............FF..", "HAT": "............NNNN"}}]}

A lane string has one character a step: '.' rest, 'g' ghost, 's' soft, 'x' normal, 'X' hard. A ratchet
string: '.' or 1, or 2..4 repeats inside the step. "fills" marks a lane's hits one by one: '.' plays
always, 'F' only during a fill (the pedal's CC 21, or the LIVE screen's black keys and SEL), 'N' never
during one. The older "fill" string does the same for a whole step, every lane at once. Lanes:
KICK, KICK2, SNARE, CLAP, HAT, OPENHAT, PEDAL, RIM, SNARE2, LOWTOM, MIDTOM, HITOM, CRASH, RIDE, SHAKER, COWBELL
(MIDTOM since 2026-10-14, where the conga was: "CONGA" still reads as it; spaces and case do not matter). div: 1/4 1/8 1/16 1/32 8T 16T 1/2 1BAR 2BAR. time: the section's meter,
n/d with n 1..16 and d 2, 4, 8 or 16 (none: 4/4); the pedal's bars, the click and the count-in follow it. A section is
whole bars of its time (a change waits for the bar line): a pickup is a short bar, a 4-step intro of 1/16 "1/4";
a section that is not gets a warning with the signature that fits. kit: the
index in the device's KIT list (0 ACOUSTIC .. as the web editor shows them). Up to 16 sections; the packed song
("SNG2": a section header of 18 bytes, the fill conditions, then a step: 2 bytes of lanes and a byte a hit) must
stay under 3800 bytes: 64 steps of three hits take about 340 bytes, so 8 full sections fit, and 16 of the usual
16..48 steps (about 60..150 bytes each).

Sending needs mido (pip install mido python-rtmidi); the transfer is the editor protocol's backup PUT
(EDITOR_PROTOCOL.md: object 16 + slot; the pedal settings: object 24)."""
import argparse
import json
import struct
import sys
import time
import zlib

HDR = [0x7D, 0x46, 0x4C]
BK_LIST, BK_GET, BK_PUT = 34, 35, 36
OBJ_SONG0, OBJ_ARRSET, OBJ_SONG8 = 16, 24, 40     # editor.c's backup objects: songs 1..8, the pedal settings, songs 9..16
SLOTS = 16


def song_obj(slot):
    if not 0 <= slot < SLOTS:
        sys.exit(f"a song slot is 0..{SLOTS - 1}")
    return OBJ_SONG0 + slot if slot < 8 else OBJ_SONG8 + slot - 8
SNG_MAGIC = b"SNG2"
HDR, SEC_HDR = 32, 18
SNG_MAX = 3800
KIT_DEFAULT = 46                                     # drums.c DRUM_REAL: MULDJORD, the layered kit (0 ACOUSTIC before)
SNG_SECTIONS = 16                                    # seq.c SNG_SECTIONS (8 until 2026-10-14)
NSTEP = 64
LANES = ["KICK", "KICK2", "SNARE", "CLAP", "HAT", "OPENHAT", "PEDAL", "RIM",
         "SNARE2", "LOWTOM", "MIDTOM", "HITOM", "CRASH", "RIDE", "SHAKER", "COWBELL"]
DIVS = ["1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1BAR", "2BAR"]
LEVELS = {"x": 0, "g": 1, "s": 2, "X": 3}            # LV_NORM, LV_GHOST, LV_SOFT, LV_HARD
CONDS = {".": 0, "F": 1, "N": 2}                     # FC_NORM, FC_FILL, FC_NOFILL
DENS = [4, 8, 2, 16]                                 # the meter byte's denominator code
STEP_U = {"1/4": 48, "1/8": 24, "1/16": 12, "1/32": 6, "8T": 16, "16T": 8, "1/2": 96, "1BAR": 192, "2BAR": 384}
                                                     # a step in 1/48 of a beat (core.h div_units / 48)


def lane_index(name):
    key = "".join(ch for ch in name.upper() if ch.isalnum())
    if key == "CONGA":                               # (lane 14 was the conga until 2026-10-14: an old song's congas on it)
        if not getattr(lane_index, "warned", False):
            print("note: lane CONGA is the mid tom now (MIDTOM); the song's conga hits play it", file=sys.stderr)
            lane_index.warned = True
        key = "MIDTOM"
    if key not in LANES:
        sys.exit(f"unknown lane {name!r}; lanes: {' '.join(LANES)}")
    return LANES.index(key)


def meter_byte(t, name):
    """a section's "time" ("7/8"; none: 4/4) as the section header's meter byte: 0 = 4/4, else the numerator
    (1..16) in bits 0..4 and the denominator's code (DENS) in bits 5..6"""
    if t is None or t == "":
        return 0
    try:
        num, den = (int(x) for x in str(t).split("/"))
    except ValueError:
        sys.exit(f"{name}: time {t!r} is not n/d")
    if not 1 <= num <= 16 or den not in DENS:
        sys.exit(f"{name}: time {t}: n 1..16, d 2, 4, 8 or 16")
    return 0 if (num, den) == (4, 4) else num | DENS.index(den) << 5


def meter_str(v):
    return None if not v & 31 else f"{v & 31}/{DENS[(v >> 5) & 3]}"


def bar_fit(steps, div, time):
    """a section is whole bars of its time signature (the device counts bars: a change waits for the bar line).
    None when it is; else (its length in bars, a Fraction, and the signature whose one bar is the pattern, "1/4",
    or None if no n/d fits)"""
    from fractions import Fraction
    num, den = (int(x) for x in str(time).split("/")) if time else (4, 4)
    length = steps * STEP_U[div]                     # in 1/48 of a beat
    bar = 192 * num // den
    if length % bar == 0:
        return None
    for d in [den] + [x for x in (4, 8, 16, 2) if x != den]:
        if length * d % 192 == 0 and 1 <= length * d // 192 <= 16:
            return Fraction(length, bar), f"{length * d // 192}/{d}"
    return Fraction(length, bar), None


def bar_warnings(spec):
    """one line per section that is not whole bars of its signature, with the signature that fits"""
    out = []
    for i, sec in enumerate(spec.get("sections", [])):
        name = str(sec.get("name", f"PART {i + 1}"))[:12]
        time = sec.get("time") or "4/4"
        try:
            fit = bar_fit(int(sec.get("steps", 16)), sec.get("div", "1/16"), time)
        except (KeyError, ValueError, ZeroDivisionError):
            continue                                 # (pack_section reports a bad div or time)
        if fit:
            bars, sig = fit
            n = f"{bars} of a {time} bar" if bars < 1 else f"{float(bars):g} bars of {time}"
            out.append(f"{name}: {sec.get('steps', 16)} steps of {sec.get('div', '1/16')} is {n}: a change waits for the "
                       f"bar line" + (f'; set "time": "{sig}" (one bar = the pattern)' if sig else ""))
    return out


def pack_section(sec, index):
    name = str(sec.get("name", f"PART {index + 1}"))[:12]
    steps = int(sec.get("steps", 16))
    div = sec.get("div", "1/16")
    if not 1 <= steps <= NSTEP:
        sys.exit(f"{name}: steps must be 1..{NSTEP}")
    if div not in DIVS:
        sys.exit(f"{name}: div must be one of {' '.join(DIVS)}")
    meter = meter_byte(sec.get("time"), name)
    hit = [[None] * 16 for _ in range(steps)]        # each lane's hit byte: level, ratchet << 2, mark << 4
    for lane, s in sec.get("lanes", {}).items():
        k = lane_index(lane)
        s = str(s)
        if len(s) > steps:
            sys.exit(f"{name}: lane {lane} has {len(s)} characters for {steps} steps")
        for i, ch in enumerate(s):
            if ch == ".":
                continue
            if ch not in LEVELS:
                sys.exit(f"{name}: lane {lane}: '{ch}' is not one of . g s x X")
            hit[i][k] = LEVELS[ch]
    for lane, s in sec.get("ratchets", {}).items():
        k = lane_index(lane)
        for i, ch in enumerate(str(s)):
            if ch in ".1":
                continue
            if ch not in "234":
                sys.exit(f"{name}: ratchets {lane}: '{ch}' is not . 1 2 3 4")
            if i < steps and hit[i][k] is not None:
                hit[i][k] |= (int(ch) - 1) << 2
    for lane, v in sec.get("fills", {}).items():    # a lane's own marks: F only during a fill, N not during one
        k = lane_index(lane)
        v = str(v)
        if len(v) > steps:
            sys.exit(f"{name}: fills {lane} has {len(v)} characters for {steps} steps")
        for i, ch in enumerate(v):
            if ch == ".":
                continue
            if ch not in "FN":
                sys.exit(f"{name}: fills {lane}: '{ch}' is not . F N")
            if hit[i][k] is not None:
                hit[i][k] |= (1 if ch == "F" else 2) << 4
    fill = bytearray((steps + 3) // 4)
    cond = str(sec.get("fill", ""))
    if len(cond) > steps:
        sys.exit(f"{name}: fill has {len(cond)} characters for {steps} steps")
    for i, ch in enumerate(cond):
        if ch not in CONDS:
            sys.exit(f"{name}: fill: '{ch}' is not . F N")
        fill[i // 4] |= CONDS[ch] << (2 * (i % 4))
    body = bytearray(fill)
    for i in range(steps):
        on = sum(1 << k for k in range(16) if hit[i][k] is not None)
        body += struct.pack("<H", on) + bytes(h for h in hit[i] if h is not None)
    size = SEC_HDR + len(body)
    out = bytearray(name.encode("ascii", "replace").ljust(12, b"\0"))
    out += bytes([steps, DIVS.index(div), 1 if sec.get("outro") else 0, meter]) + struct.pack("<H", size)
    return bytes(out + body)


def pack(spec):
    secs = spec.get("sections", [])
    if not 1 <= len(secs) <= SNG_SECTIONS:
        sys.exit(f"a song has 1 to {SNG_SECTIONS} sections")
    bpm = int(spec.get("bpm", 120))
    if not 40 <= bpm <= 240:
        sys.exit("bpm must be 40..240")
    kit = int(spec.get("kit", KIT_DEFAULT))
    swing = int(spec.get("swing", 0))                # 0 straight .. 100 the most (MPC 50 .. 75 %): seq.c swing_units
    if not 0 <= swing <= 100:
        sys.exit("swing must be 0..100")
    name = str(spec.get("name", "SONG"))[:12].encode("ascii", "replace").ljust(12, b"\0")
    hdr = SNG_MAGIC + name + struct.pack("<HBBBBB", bpm, kit, swing, int(spec.get("drum_level", 100)),
                                          int(spec.get("drum_reverb", 20)), len(secs)) + bytes(9)
    body = b"".join(pack_section(s, i) for i, s in enumerate(secs))
    for w in bar_warnings(spec):                     # (a warning: the device plays it, but times it in whole bars)
        print(f"warning: {w}", file=sys.stderr)
    blob = hdr + body
    if len(blob) > SNG_MAX:
        sys.exit(f"the packed song is {len(blob)} bytes; at most {SNG_MAX} (fewer or shorter sections)")
    return blob


def sections(blob):
    """the packed song's sections: (name, steps, div, flags, meter, offset of the steps, size), as sng.c walks them"""
    nsec = blob[22]
    pos = HDR
    out = []
    for _ in range(nsec):
        steps, div, flags, meter = blob[pos + 12:pos + 16]
        size = blob[pos + 16] | blob[pos + 17] << 8
        out.append((blob[pos:pos + 12].rstrip(b"\0").decode("ascii", "replace"), steps, div, flags, meter,
                    pos + SEC_HDR + (steps + 3) // 4, size))
        pos += size
    return out


def describe(blob):
    name = blob[4:16].rstrip(b"\0").decode("ascii", "replace")
    bpm, kit, swing, lvl, rev, nsec = struct.unpack("<HBBBBB", blob[16:23])
    print(f"{name}: {bpm} BPM, kit {kit}, swing {swing}, drums {lvl} / reverb {rev}, {nsec} sections, {len(blob)} bytes")
    for k, (sname, steps, div, flags, meter, q, size) in enumerate(sections(blob)):
        hits = 0
        for _ in range(steps):
            n = bin(blob[q] | blob[q + 1] << 8).count("1")
            hits += n
            q += 2 + n
        print(f"  {k}: {sname:12s} {steps:2d} steps {DIVS[div]:5s} {meter_str(meter) or '4/4':5s} {hits:3d} hits {size:4d} bytes"
              f"{'  OUTRO' if flags & 1 else ''}")


def pack7(b):
    out = []
    for i in range(0, len(b), 7):
        g = b[i:i + 7]
        out.append(sum(((x >> 7) & 1) << j for j, x in enumerate(g)))
        out += [x & 0x7F for x in g]
    return out


def unpack7(a):
    out = bytearray()
    i = 0
    while i < len(a):
        top = a[i]
        g = a[i + 1:i + 8]
        out += bytes(x | ((top >> j) & 1) << 7 for j, x in enumerate(g))
        i += 8
    return bytes(out)


def u35(v):
    return [(v >> (7 * i)) & 127 for i in range(5)]


class Link:
    def __init__(self):
        try:
            import mido
        except ImportError:
            sys.exit("needs mido: pip install mido python-rtmidi")
        self.mido = mido
        names = ("Felucca", "SLOOP", "FM-1")
        outs = [n for n in mido.get_output_names() if any(x in n for x in names)]
        ins = [n for n in mido.get_input_names() if any(x in n for x in names)]
        if not outs or not ins:
            sys.exit("no MIDI port of the FM-1: connect it by USB (a data cable)")
        self.o = mido.open_output(outs[0])
        self.i = mido.open_input(ins[0])
        for _ in range(3):
            try:
                self.req(1, [], 1.0)
                break
            except TimeoutError:
                pass

    def req(self, cmd, args, timeout=3.0):
        for _ in self.i.iter_pending():
            pass
        self.o.send(self.mido.Message("sysex", data=HDR + [cmd] + list(args)))
        t0 = time.time()
        while time.time() - t0 < timeout:
            for m in self.i.iter_pending():
                if m.type == "sysex" and list(m.data[:4]) == HDR + [cmd]:
                    return list(m.data[4:])
            time.sleep(0.002)
        raise TimeoutError(f"no reply to cmd {cmd}")

    def put(self, obj, blob):
        """the backup PUT: begin, data in 256-byte pieces, commit (rc 0 ok)"""
        r = self.req(BK_PUT, [0, obj] + u35(len(blob)) + u35(zlib.crc32(blob)))
        if r[2]:
            sys.exit(f"begin refused: rc {r[2]} (3: stop the song first)")
        for off in range(0, len(blob), 256):
            r = self.req(BK_PUT, [1, obj] + u35(off) + pack7(blob[off:off + 256]))
            if r[2]:
                sys.exit(f"data refused at {off}: rc {r[2]}")
        r = self.req(BK_PUT, [2, obj], 8.0)
        return r[2]

    def get(self, obj):
        self.req(BK_LIST, [], 8.0)
        out = bytearray()
        off = 0
        while True:
            r = self.req(BK_GET, [obj] + u35(off) + [256 & 127, 256 >> 7])
            if r[1] == 1 and off:
                break                                   # past the end
            if r[1]:
                sys.exit(f"GET refused: rc {r[1]} (an empty slot?)")
            n = r[7] | r[8] << 7
            data = unpack7(r[9:])[:n]
            out += data
            off += n
            if n < 256:
                break
        return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("json", nargs="?", help="the song (JSON)")
    ap.add_argument("--out", help="write the packed song here")
    ap.add_argument("--put", type=int, metavar="SLOT", help="send it into song slot 0..15")
    ap.add_argument("--get", type=int, metavar="SLOT", help="read song slot 0..15 back")
    ap.add_argument("--erase", type=int, metavar="SLOT", help="erase song slot 0..15")
    ap.add_argument("--pedal-channel", type=int, metavar="CH", help="0 off, 1..16, 17 any")
    ap.add_argument("--count-in", type=int, metavar="BARS", help="bars of clicks before a start from stopped: 0, 1, 2")
    a = ap.parse_args()
    blob = None
    if a.json:
        with open(a.json) as f:
            blob = pack(json.load(f))
        describe(blob)
    if a.get is not None:
        blob = Link().get(song_obj(a.get))
        if not blob.startswith(SNG_MAGIC):
            sys.exit("not a song")
        describe(blob)
    if a.out and blob:
        with open(a.out, "wb") as f:
            f.write(blob)
        print(f"wrote {a.out}")
    if a.put is not None:
        if blob is None:
            sys.exit("--put needs the JSON")
        rc = Link().put(song_obj(a.put), blob)
        print("sent" if rc == 0 else f"refused: rc {rc} (2: not a valid song, 3: stop the song first, 4: flash)")
    if a.erase is not None:
        rc = Link().put(song_obj(a.erase), b"")
        print("erased" if rc == 0 else f"refused: rc {rc}")
    if a.pedal_channel is not None or a.count_in is not None:
        if a.pedal_channel is not None and not 0 <= a.pedal_channel <= 17:
            sys.exit("the pedal channel is 0..17")
        if a.count_in is not None and not 0 <= a.count_in <= 2:
            sys.exit("the count-in is 0, 1 or 2 bars")
        link = Link()
        try:
            cur = link.get(OBJ_ARRSET)                       # the settings as they are (the other one kept)
        except SystemExit:
            cur = b""
        ch, count = (cur[4], cur[5]) if cur[:4] == b"ARS1" and len(cur) >= 6 else (16, 0)
        if a.pedal_channel is not None:
            ch = a.pedal_channel
        if a.count_in is not None:
            count = a.count_in
        rest = cur[6:16] if cur[:4] == b"ARS1" and len(cur) >= 16 else bytes(10)   # (SETUP's autoload and last song kept)
        rc = link.put(OBJ_ARRSET, b"ARS1" + bytes([ch, count]) + rest)
        print(f"pedal channel {ch}, count-in {count} bar(s): set" if rc == 0 else f"refused: rc {rc}")
    if not any(x is not None for x in (a.json, a.get, a.erase, a.pedal_channel, a.count_in)):
        ap.print_help()


if __name__ == "__main__":
    main()
