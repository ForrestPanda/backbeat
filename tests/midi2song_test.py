#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""fm1-drummer: tools/gp2song.py on Standard MIDI Files written here (no library: the writer below is a page, as the
reader in gp2song.py): the twin of gp2song_test.py's synthetic song as a type-1 file (markers, a verse of 8 bars on a
2-bar pattern with a fill in the last bar, a chorus with a flam written as a grace note, a drag as two 32nds, an accent
as velocity 120, a tambourine on a second track, an outro, a verse repeated and skipped), then a type-0 file with a
7/8 section, a tempo change (ignored), humanised notes (snapped), and the drums picked by the track's name when no
channel 10 plays; a file with no drums fails with the tracks listed. The JSON is then packed by fm1_song.py."""
import json
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import fm1_song  # noqa: E402
import gp2song  # noqa: E402

TPQ = 480
BAR = 4 * TPQ


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.insert(0, 0x80 | n & 0x7F)
        n >>= 7
    return bytes(out)


def meta(typ, payload):
    return bytes([0xFF, typ]) + vlq(len(payload)) + payload


def track_chunk(events):
    """events: [(tick, bytes)] in any order -> an MTrk chunk with the end-of-track meta"""
    out, last = b"", 0
    for tick, data in sorted(events, key=lambda e: e[0]):
        out += vlq(tick - last) + data
        last = tick
    out += vlq(0) + meta(0x2F, b"")
    return b"MTrk" + struct.pack(">I", len(out)) + out


def smf(fmt, tracks, tpq=TPQ):
    return b"MThd" + struct.pack(">IHHH", 6, fmt, len(tracks), tpq) + b"".join(track_chunk(t) for t in tracks)


def note(events, tick, n, vel=95, chan=9, length=TPQ // 8):
    events.append((tick, bytes([0x90 | chan, n, vel])))
    events.append((tick + length, bytes([0x80 | chan, n, 0])))


def make_synthetic():
    """gp2song_test.make_song as MIDI: bar i at i * BAR; 16th s at s * TPQ // 4"""
    meta_t = [(0, meta(0x03, b"Synthetic")), (0, meta(0x51, (500000).to_bytes(3, "big"))), (0, meta(0x58, bytes([4, 2, 24, 8])))]
    for at, title in ((0, b"Verse"), (8, b"Chorus"), (12, b"Outro"), (14, b"Verse 2")):
        meta_t.append((at * BAR, meta(0x06, title)))
    drums, perc = [(0, meta(0x03, b"Drums"))], [(0, meta(0x03, b"Perc"))]

    def s16(bar, s):
        return bar * BAR + s * TPQ // 4

    def verse(bar, variant, fill=False):
        for s in range(0, 12 if fill else 16, 2):
            note(drums, s16(bar, s), 42, 63)                       # hats, soft
        note(drums, s16(bar, 0), 36); note(drums, s16(bar, 8), 36)
        note(drums, s16(bar, 4), 38); note(drums, s16(bar, 12 if not fill else 4), 38)
        if variant:
            note(drums, s16(bar, 10), 36)
        if fill:
            for s, n in ((12, 50), (13, 50), (14, 45), (15, 45)):
                note(drums, s16(bar, s), n)

    for i in range(8):
        verse(i, i % 2, fill=i == 7)
    for i in range(8, 12):                                         # the chorus: 4 identical bars
        for s in range(0, 16, 2):
            note(drums, s16(i, s), 46)
        note(drums, s16(i, 0), 36, 120); note(drums, s16(i, 0), 49, 120)   # the accents: velocity 120
        note(drums, s16(i, 4) - 30, 38, 60); note(drums, s16(i, 4), 38)    # a grace note 30 ticks before the snare: a flam
        note(drums, s16(i, 8), 36)
        note(drums, s16(i, 12), 38); note(drums, s16(i, 12) + TPQ // 8, 38)   # two 32nds: a ratchet
        for s in range(0, 16, 4):
            note(perc, s16(i, s), 54, 40)                          # tambourine (ghost) on every beat, the second track
    for i in (12, 13):
        note(drums, s16(i, 0), 36); note(drums, s16(i, 0), 49)
    for i in range(14, 18):
        verse(i, i % 2)
    return smf(1, [meta_t, drums, perc])


def make_odd():
    """type 0, one track, the drums on channel 10: four bars of 7/8 under a marker, then 4/4 with the outro; a tempo
    change in the middle (ignored); every note 10..18 ticks off the grid (a humanised part: snapped back)"""
    ev = [(0, meta(0x03, b"Odd one")), (0, meta(0x51, (600000).to_bytes(3, "big"))), (0, meta(0x58, bytes([7, 3, 24, 8]))),
          (0, meta(0x06, b"Verse"))]
    bar7 = 7 * TPQ // 2
    jit = [11, -14, 17, -10, 13, -18, 15, -12]
    k = 0

    def put(tick, n, vel=95):
        nonlocal k
        note(ev, max(0, tick + jit[k % len(jit)]), n, vel)
        k += 1

    for i in range(4):
        b = i * bar7
        put(b, 36); put(b + 3 * TPQ // 2, 36)                    # kick on 1 and on the 4th 8th
        for e in range(7):
            put(b + e * TPQ // 2, 42, 63)
        put(b + TPQ, 38); put(b + 5 * TPQ // 2, 38)
    at = 4 * bar7
    ev.append((at, meta(0x58, bytes([4, 2, 24, 8]))))
    ev.append((at, meta(0x06, b"Outro")))
    ev.append((at, meta(0x51, (400000).to_bytes(3, "big"))))      # 150 BPM from here: ignored
    for i in range(2):
        put(at + i * BAR, 36); put(at + i * BAR, 49)
    return smf(0, [ev])


def make_named():
    """no channel 10: the drums on channel 1 of a track called 'Drum Kit', a bass on another; no markers (auto sections)"""
    t0 = [(0, meta(0x03, b"Named")), (0, meta(0x51, (500000).to_bytes(3, "big")))]
    bass = [(0, meta(0x03, b"Bass"))]
    kit = [(0, meta(0x03, b"Drum Kit"))]
    for i in range(8):
        for s in range(0, 16, 2):
            note(kit, i * BAR + s * TPQ // 4, 42, 70, chan=0)
        note(kit, i * BAR, 36, chan=0); note(kit, i * BAR + 2 * TPQ, 36, chan=0)
        note(kit, i * BAR + TPQ, 38, chan=0); note(kit, i * BAR + 3 * TPQ, 38, chan=0)
        note(bass, i * BAR, 40, chan=1, length=TPQ)
    return smf(1, [t0, bass, kit])


def run(data, name, *args):
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, name)
        with open(src, "wb") as f:
            f.write(data)
        out = os.path.join(d, "out.json")
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "gp2song.py"), src, "--out", out, *args],
                           capture_output=True, text=True)
        spec = None
        if r.returncode == 0:
            with open(out) as f:
                spec = json.load(f)
        return r, spec


def main():
    r, spec = run(make_synthetic(), "synthetic.mid")
    print(r.stdout.strip())
    assert r.returncode == 0, r.stderr
    assert "type 1, drums: channel 10: track 2 'Drums'" in r.stdout and "track 3 'Perc'" in r.stdout, r.stdout
    names = [s["name"] for s in spec["sections"]]
    assert names == ["VERSE", "CHORUS", "OUTRO"], names
    assert spec["name"] == "SYNTHETIC" and spec["bpm"] == 120, (spec["name"], spec["bpm"])
    v, c, o = spec["sections"]
    assert v["steps"] == 32 and v["div"] == "1/16", v
    assert v["lanes"]["KICK"][:27] == "x.......x.......x.......x.x", v["lanes"]["KICK"]
    assert v["lanes"]["HAT"].startswith("s.s.s.s.s.s.s.s.s.s.s.s."), v["lanes"]["HAT"]
    assert v["lanes"]["SNARE"][:16] == "....x.......x...", v["lanes"]["SNARE"]
    fb = v["fillbar"]
    assert fb["steps"] == 16 and fb["lanes"]["HITOM"] == "............xx.." and fb["lanes"]["MIDTOM"] == "..............xx", fb["lanes"]
    assert fb["lanes"]["HAT"] == "s.s.s.s.s.s....." and fb["lanes"]["SNARE"] == "....x..........." and fb["lanes"]["KICK"] == "x.......x.x.....", fb["lanes"]
    assert c["steps"] == 16 and c["lanes"]["CRASH"][0] == "X" and c["lanes"]["KICK"][0] == "X", c["lanes"]
    assert c["lanes"]["SNARE"] == "....x.......x..." and c["ratchets"]["SNARE"][12] == "2", (c["lanes"]["SNARE"], c.get("ratchets"))
    assert c["flams"]["SNARE"] == "....f...........", c.get("flams")                     # the grace note: a flam, one hit
    assert "flams" not in v and "flams" not in o, (v.get("flams"), o.get("flams"))
    assert c["lanes"]["SHAKER"] == "g...g...g...g...", c["lanes"].get("SHAKER")
    assert c["lanes"]["OPENHAT"] == "x.x.x.x.x.x.x.x.", c["lanes"]["OPENHAT"]
    assert o.get("outro") is True and o["steps"] == 16 and o["lanes"]["CRASH"][0] == "x", o
    blob = fm1_song.pack(spec)
    assert blob[:4] == b"SNG2" and len(blob) < 3800
    # the same song as the Guitar Pro twin would give it: the fields the two readers share
    bars = gp2song.convert(make_synthetic(), detail=True, log=lambda s: None)["bars"]
    assert len(bars) == 18 and bars[7]["lanes"]["HITOM"] == "............xx.." and bars[8]["flams"]["SNARE"] == "....f...........", (len(bars), bars[7], bars[8])
    print(f"midi2song: type 1: verse (2-bar pattern, a fill bar of its own), chorus (velocity 120 hard, a grace note a flam, a drag a ratchet, "
          f"the second track's tambourine), outro, the repeat skipped, {len(blob)} bytes packed PASS")

    r, spec = run(make_odd(), "odd.mid")
    print(r.stdout.strip())
    assert r.returncode == 0, r.stderr
    assert spec["bpm"] == 100 and "1 tempo change ignored" in r.stdout and spec["name"] == "ODD ONE", (spec["bpm"], r.stdout)
    seven, four = spec["sections"]
    assert seven["time"] == "7/8" and seven["steps"] == 14 and seven["div"] == "1/16", seven
    assert seven["lanes"]["KICK"] == "x.....x......." and seven["lanes"]["HAT"] == "s.s.s.s.s.s.s." and seven["lanes"]["SNARE"] == "....x.....x...", seven["lanes"]
    assert four.get("outro") and "time" not in four and four["steps"] == 16 and four["lanes"]["CRASH"] == "x...............", four
    blob = fm1_song.pack(spec)
    assert fm1_song.sections(blob)[0][4] == 7 | 1 << 5 and fm1_song.sections(blob)[1][4] == 0
    print("midi2song: type 0: a 7/8 section from the time signature, humanised notes back on the grid, the tempo change ignored PASS")

    r, spec = run(make_named(), "named.mid")
    print(r.stdout.strip())
    assert r.returncode == 0, r.stderr
    assert "the track named for drums: track 3 'Drum Kit'" in r.stdout and "no markers" in r.stdout, r.stdout
    assert len(spec["sections"]) == 1 and spec["sections"][0]["lanes"]["KICK"] == "x.......x......." and "SNARE2" not in spec["sections"][0]["lanes"], spec["sections"]
    r, spec = run(make_named(), "named.mid", "--track", "2")
    assert r.returncode == 0 and spec["sections"][0]["lanes"] == {"SNARE": "x..............."}, (r.stderr, spec and spec["sections"])   # the bass's E (40) read as a drum: the user asked for that track
    r, spec = run(smf(1, [[(0, meta(0x03, b"Piano"))] + [(i * TPQ, bytes([0x90, 60 + i, 90])) for i in range(4)]]), "none.mid")
    assert r.returncode != 0 and "no drum notes (channel 10)" in r.stderr and "1 'Piano'" in r.stderr, (r.returncode, r.stderr)
    r, spec = run(b"RIFF" + bytes(20), "junk.mid")
    assert r.returncode != 0, r.stdout
    print("midi2song: no channel 10: the track named for drums; --track picks one; no drums: the tracks listed; not a file PASS")


if __name__ == "__main__":
    main()
