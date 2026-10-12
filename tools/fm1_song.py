#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""fm1-arranger: songs for the FM-1 from a JSON file (firmware/src/sng.c).

    tools/fm1_song.py song.json                     check it, print its sections and size
    tools/fm1_song.py song.json --out song.bin      the packed song (as the device stores it)
    tools/fm1_song.py song.json --put 0             pack and send it into song slot 0 (the FM-1 stopped, by USB)
    tools/fm1_song.py --get 0 --out song.bin        read slot 0 back (packed)
    tools/fm1_song.py --erase 0                     erase slot 0
    tools/fm1_song.py --pedal-channel 16            the pedal channel: 0 off, 1..16, 17 any (kept through power-off)

The JSON (see docs/design.md):

    {"name": "MY SONG", "bpm": 120, "kit": 0, "swing": 50, "drum_level": 100, "drum_reverb": 20,
     "sections": [
       {"name": "VERSE", "steps": 16, "div": "1/16", "outro": false,
        "lanes": {"KICK": "x...x...x...x...", "SNARE": "....x.......x...", "HAT": "x.x.x.x.x.x.x.x."},
        "ratchets": {"SNARE": "............2..."},
        "fills": {"HITOM": "............FF..", "HAT": "............NNNN"}}]}

A lane string has one character a step: '.' rest, 'g' ghost, 'o' soft, 'x' normal, 'X' hard. A ratchet
string: '.' or 1, or 2..4 repeats inside the step. "fills" marks a lane's hits one by one: '.' plays
always, 'F' only during a fill (the pedal's transition, CC 21, or the device's GLO + key 9 / 10), 'N' never
during one. The older "fill" string does the same for a whole step, every lane at once. Lanes:
KICK, KICK2, SNARE, CLAP, HAT, OPENHAT, PEDAL, RIM, SNARE2, LOWTOM, HITOM, CRASH, RIDE, SHAKER, CONGA,
COWBELL (spaces and case do not matter). div: 1/4 1/8 1/16 1/32 8T 16T 1/2 1BAR 2BAR. kit: the index in the
device's KIT list (0 ACOUSTIC .. as the web editor shows them). Up to 8 sections; the packed song must stay
under 3800 bytes (about 8 sections of 48 steps, or 5 of 64).

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
OBJ_SONG0, OBJ_ARRSET = 16, 24
SNG_MAGIC = b"SNG1"
SNG_MAX = 3800
NSTEP = 64
LANES = ["KICK", "KICK2", "SNARE", "CLAP", "HAT", "OPENHAT", "PEDAL", "RIM",
         "SNARE2", "LOWTOM", "HITOM", "CRASH", "RIDE", "SHAKER", "CONGA", "COWBELL"]
DIVS = ["1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1BAR", "2BAR"]
LEVELS = {"x": 0, "g": 1, "o": 2, "X": 3}            # LV_NORM, LV_GHOST, LV_SOFT, LV_HARD
CONDS = {".": 0, "F": 1, "N": 2}                     # FC_NORM, FC_FILL, FC_NOFILL


def lane_index(name):
    key = "".join(ch for ch in name.upper() if ch.isalnum())
    if key not in LANES:
        sys.exit(f"unknown lane {name!r}; lanes: {' '.join(LANES)}")
    return LANES.index(key)


def pack_section(sec, index):
    name = str(sec.get("name", f"PART {index + 1}"))[:12]
    steps = int(sec.get("steps", 16))
    div = sec.get("div", "1/16")
    if not 1 <= steps <= NSTEP:
        sys.exit(f"{name}: steps must be 1..{NSTEP}")
    if div not in DIVS:
        sys.exit(f"{name}: div must be one of {' '.join(DIVS)}")
    flags = (1 if sec.get("outro") else 0)
    on = [[0, 0] for _ in range(steps)]
    lvl = [[0] * 4 for _ in range(steps)]
    rat = [[0] * 4 for _ in range(steps)]
    for lane, s in sec.get("lanes", {}).items():
        k = lane_index(lane)
        s = str(s)
        if len(s) > steps:
            sys.exit(f"{name}: lane {lane} has {len(s)} characters for {steps} steps")
        for i, ch in enumerate(s):
            if ch == ".":
                continue
            if ch not in LEVELS:
                sys.exit(f"{name}: lane {lane}: '{ch}' is not one of . g o x X")
            on[i][k >> 3] |= 1 << (k & 7)
            lvl[i][k >> 2] |= LEVELS[ch] << (2 * (k & 3))
    for lane, s in sec.get("ratchets", {}).items():
        k = lane_index(lane)
        for i, ch in enumerate(str(s)):
            if ch in ".1":
                continue
            if ch not in "234":
                sys.exit(f"{name}: ratchets {lane}: '{ch}' is not . 1 2 3 4")
            rat[i][k >> 2] |= (int(ch) - 1) << (2 * (k & 3))
    marks = sec.get("fills", {})                     # a lane's own marks: F only during a fill, N not during one
    fonly = [0] * steps
    fno = [0] * steps
    for lane, v in marks.items():
        k = lane_index(lane)
        v = str(v)
        if len(v) > steps:
            sys.exit(f"{name}: fills {lane} has {len(v)} characters for {steps} steps")
        for i, ch in enumerate(v):
            if ch == ".":
                continue
            if ch not in "FN":
                sys.exit(f"{name}: fills {lane}: '{ch}' is not . F N")
            (fonly if ch == "F" else fno)[i] |= 1 << k
    has_marks = any(fonly) or any(fno)
    fill = bytearray((steps + 3) // 4)
    cond = str(sec.get("fill", ""))
    if len(cond) > steps:
        sys.exit(f"{name}: fill has {len(cond)} characters for {steps} steps")
    for i, ch in enumerate(cond):
        if ch not in CONDS:
            sys.exit(f"{name}: fill: '{ch}' is not . F N")
        fill[i // 4] |= CONDS[ch] << (2 * (i % 4))
    out = bytearray(name.encode("ascii", "replace").ljust(12, b"\0"))
    out += bytes([steps, DIVS.index(div), flags | (2 if has_marks else 0), 0])
    out += fill
    for i in range(steps):
        out += bytes(on[i]) + bytes(lvl[i]) + bytes(rat[i])
    if has_marks:
        for i in range(steps):
            out += struct.pack("<HH", fonly[i], fno[i])
    return bytes(out)


def pack(spec):
    secs = spec.get("sections", [])
    if not 1 <= len(secs) <= 8:
        sys.exit("a song has 1 to 8 sections")
    bpm = int(spec.get("bpm", 120))
    if not 40 <= bpm <= 240:
        sys.exit("bpm must be 40..240")
    kit = int(spec.get("kit", 0))
    swing = int(spec.get("swing", 50))
    if not 0 <= swing <= 100:
        sys.exit("swing must be 0..100")
    name = str(spec.get("name", "SONG"))[:12].encode("ascii", "replace").ljust(12, b"\0")
    hdr = SNG_MAGIC + name + struct.pack("<HBBBBB", bpm, kit, swing, int(spec.get("drum_level", 100)),
                                          int(spec.get("drum_reverb", 20)), len(secs)) + bytes(9)
    body = b"".join(pack_section(s, i) for i, s in enumerate(secs))
    blob = hdr + body
    if len(blob) > SNG_MAX:
        sys.exit(f"the packed song is {len(blob)} bytes; at most {SNG_MAX} (fewer or shorter sections)")
    return blob


def describe(blob):
    name = blob[4:16].rstrip(b"\0").decode("ascii", "replace")
    bpm, kit, swing, lvl, rev, nsec = struct.unpack("<HBBBBB", blob[16:23])
    print(f"{name}: {bpm} BPM, kit {kit}, swing {swing}, drums {lvl} / reverb {rev}, {nsec} sections, {len(blob)} bytes")
    pos = 32
    for k in range(nsec):
        sname = blob[pos:pos + 12].rstrip(b"\0").decode("ascii", "replace")
        steps, div, flags = blob[pos + 12], blob[pos + 13], blob[pos + 14]
        nf = (steps + 3) // 4
        hits = sum(bin(blob[pos + 16 + nf + i * 10] | blob[pos + 16 + nf + i * 10 + 1] << 8).count("1") for i in range(steps))
        print(f"  {k}: {sname:12s} {steps:2d} steps {DIVS[div]:5s} {hits:3d} hits{'  OUTRO' if flags & 1 else ''}{'  lane marks' if flags & 2 else ''}")
        pos += 16 + nf + steps * 10 + (steps * 4 if flags & 2 else 0)


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
    ap.add_argument("--put", type=int, metavar="SLOT", help="send it into song slot 0..7")
    ap.add_argument("--get", type=int, metavar="SLOT", help="read song slot 0..7 back")
    ap.add_argument("--erase", type=int, metavar="SLOT", help="erase song slot 0..7")
    ap.add_argument("--pedal-channel", type=int, metavar="CH", help="0 off, 1..16, 17 any")
    a = ap.parse_args()
    blob = None
    if a.json:
        with open(a.json) as f:
            blob = pack(json.load(f))
        describe(blob)
    if a.get is not None:
        blob = Link().get(OBJ_SONG0 + a.get)
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
        rc = Link().put(OBJ_SONG0 + a.put, blob)
        print("sent" if rc == 0 else f"refused: rc {rc} (2: not a valid song, 3: stop the song first, 4: flash)")
    if a.erase is not None:
        rc = Link().put(OBJ_SONG0 + a.erase, b"")
        print("erased" if rc == 0 else f"refused: rc {rc}")
    if a.pedal_channel is not None:
        if not 0 <= a.pedal_channel <= 17:
            sys.exit("the pedal channel is 0..17")
        rc = Link().put(OBJ_ARRSET, b"ARS1" + bytes([a.pedal_channel]) + bytes(11))
        print("pedal channel set" if rc == 0 else f"refused: rc {rc}")
    if not any(x is not None for x in (a.json, a.get, a.erase, a.pedal_channel)):
        ap.print_help()


if __name__ == "__main__":
    main()
