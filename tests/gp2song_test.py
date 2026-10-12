#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""fm1-arranger: tools/gp2song.py on a Guitar Pro file written here with PyGuitarPro (no copyrighted
tab needed): markers, a verse whose 8 bars repeat a 2-bar pattern with a fill in the last bar, a chorus
of 4 identical bars with a flam (two 32nds) and an accent, an outro; a second percussion track
(tambourine) merged in; a section repeating the verse skipped. The JSON is then packed by fm1_song.py."""
import json
import os
import subprocess
import sys
import tempfile

import guitarpro as gp

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import fm1_song  # noqa: E402

Q = 960


def emit_bar(track, bar, events):
    """events: {pos32: [(gm note, velocity, accent)]} -> sequential beats (notes and rests) of one 4/4 bar"""
    m = track.measures[bar]
    voice = m.voices[0]
    voice.beats = []

    def pow2(n):
        d = 32
        while d > n:
            d //= 2
        return d

    def add(pos, length, notes):
        b = gp.Beat(voice, start=m.header.start + pos * (Q // 8), duration=gp.Duration(value=32 // length))
        b.status = gp.BeatStatus.normal if notes else gp.BeatStatus.rest
        for k, (value, vel, accent) in enumerate(notes):   # one note a string: the file stores a string mask
            n = gp.Note(b, value=value, velocity=vel, string=k + 1, type=gp.NoteType.normal)
            n.effect.accentuatedNote = accent
            b.notes.append(n)
        voice.beats.append(b)

    def rests(frm, to):
        while frm < to:
            d = pow2(to - frm)
            add(frm, d, [])
            frm += d

    positions = sorted(events)
    cur = 0
    for i, p in enumerate(positions):
        rests(cur, p)
        nxt = positions[i + 1] if i + 1 < len(positions) else 32
        d = pow2(nxt - p)
        add(p, d, events[p])
        cur = p + d
        rests(cur, nxt)
        cur = nxt
    rests(cur, 32)


def make_song():
    song = gp.Song()
    song.title = "Synthetic"
    song.tempo = 120
    tracks = []
    for num, name in ((1, "Drums"), (2, "Perc")):
        t = gp.Track(song, number=num)
        t.name = name
        t.isPercussionTrack = True
        t.channel.channel = 9
        t.strings = [gp.GuitarString(i + 1, 0) for i in range(6)]
        t.measures = []                            # (a new Track comes with one measure: ours start from none)
        tracks.append(t)
    drums, perc = tracks
    song.tracks = tracks
    nbars = 8 + 4 + 2 + 4                            # verse, chorus, outro, verse again
    song.measureHeaders = []                       # (a new Song comes with one header too)
    for i in range(nbars):
        h = gp.MeasureHeader(number=i + 1, start=Q + i * 4 * Q)
        for at, title in ((0, "Verse"), (8, "Chorus"), (12, "Outro"), (14, "Verse 2")):
            if i == at:
                h.marker = gp.Marker(title=title)
        song.measureHeaders.append(h)
        for t in tracks:
            m = gp.Measure(t, h)
            m.voices = [gp.Voice(m)]
            t.measures.append(m)

    def ev():
        return {}

    def put(e, pos16, value, vel=95, accent=False, pos32=None):
        e.setdefault(pos32 if pos32 is not None else pos16 * 2, []).append((value, vel, accent))

    def verse(variant, fill=False):
        e = ev()
        for s in range(0, 12 if fill else 16, 2):
            put(e, s, 42, vel=63)                  # hats, soft
        put(e, 0, 36); put(e, 8, 36)
        put(e, 4, 38); put(e, 12 if not fill else 4, 38)
        if variant:
            put(e, 10, 36)                         # the second bar of the pair: an extra kick
        if fill:
            for s, n in ((12, 50), (13, 50), (14, 45), (15, 45)):
                put(e, s, n)                       # toms on the last beat, no hats there
        return e

    for i in range(8):
        emit_bar(drums, i, verse(i % 2, fill=i == 7))
        emit_bar(perc, i, ev())
    for i in range(8, 12):                         # the chorus: 4 identical bars
        e = ev()
        for s in range(0, 16, 2):
            put(e, s, 46)
        put(e, 0, 36, accent=True); put(e, 0, 49, accent=True)
        put(e, 4, 38); put(e, 8, 36)
        put(e, 12, 38); put(e, 12, 38, pos32=25)   # a flam: 32nds 24 and 25
        emit_bar(drums, i, e)
        p = ev()
        for s in range(0, 16, 4):
            put(p, s, 54, vel=40)                  # tambourine (ghost) on every beat, the second track
        emit_bar(perc, i, p)
    for i in (12, 13):                             # the outro
        e = ev()
        put(e, 0, 36); put(e, 0, 49)
        emit_bar(drums, i, e)
        emit_bar(perc, i, ev())
    for i in range(14, 18):                        # the verse again: skipped as a duplicate
        emit_bar(drums, i, verse(i % 2))
        emit_bar(perc, i, ev())
    return song


def main():
    with tempfile.TemporaryDirectory() as d:
        gpf = os.path.join(d, "synthetic.gp4")
        gp.write(make_song(), gpf)
        out = os.path.join(d, "synthetic.json")
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "gp2song.py"), gpf, "--out", out],
                           capture_output=True, text=True)
        print(r.stdout.strip())
        assert r.returncode == 0, r.stderr
        with open(out) as f:
            spec = json.load(f)
    names = [s["name"] for s in spec["sections"]]
    assert names == ["VERSE", "CHORUS", "OUTRO"], names
    assert spec["bpm"] == 120
    v, c, o = spec["sections"]
    assert v["steps"] == 32 and v["div"] == "1/16", v
    assert v["lanes"]["KICK"][:27] == "x.......x.......x.......x.x", v["lanes"]["KICK"]
    assert v["lanes"]["HAT"].startswith("o.o.o.o.o.o.o.o.o.o.o.o."), v["lanes"]["HAT"]      # soft hats
    assert v["lanes"]["SNARE"][:16] == "....x.......x...", v["lanes"]["SNARE"]
    # the fill's toms (16ths 12..15) are marked F lane by lane; the pattern's hats and snare on 12 and 14, which the
    # fill leaves out, are marked N; nothing is dropped
    assert "fill" not in v and v["fills"]["HITOM"][28:32] == "FF.." and v["fills"]["LOWTOM"][28:32] == "..FF", v.get("fills")
    assert v["lanes"]["HITOM"][28:30] == "xx" and v["lanes"]["LOWTOM"][30:32] == "xx", (v["lanes"].get("HITOM"), v["lanes"].get("LOWTOM"))
    assert v["lanes"]["HAT"][28:32] == "o.o." and v["fills"]["HAT"][28:32] == "N.N." and v["fills"]["SNARE"][28] == "N", (v["lanes"]["HAT"], v["fills"].get("HAT"), v["fills"].get("SNARE"))
    assert c["steps"] == 16 and c["lanes"]["CRASH"][0] == "X" and c["lanes"]["KICK"][0] == "X", c["lanes"]
    assert c["lanes"]["SNARE"][12] == "x" and c.get("ratchets", {}).get("SNARE", "")[12] == "2", c.get("ratchets")
    assert c["lanes"]["SHAKER"] == "g...g...g...g...", c["lanes"].get("SHAKER")              # the second track, ghost
    assert c["lanes"]["OPENHAT"] == "x.x.x.x.x.x.x.x.", c["lanes"]["OPENHAT"]
    assert o.get("outro") is True and o["steps"] == 16 and o["lanes"]["CRASH"][0] == "x", o
    assert "VERSE 2" not in names
    blob = fm1_song.pack(spec)
    assert blob[:4] == b"SNG1" and len(blob) < 3800
    print(f"gp2song: verse (2-bar pattern, fill as lane marks), chorus (accent, flam, tambourine), outro, duplicate skipped, {len(blob)} bytes packed PASS")


if __name__ == "__main__":
    main()
