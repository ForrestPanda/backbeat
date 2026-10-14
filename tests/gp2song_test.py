#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""fm1-drummer: tools/gp2song.py on a Guitar Pro file written here with PyGuitarPro (no copyrighted
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


def emit_bar(track, bar, events, n32=32):
    """events: {pos32: [(gm note, velocity, accent)]} -> sequential beats (notes and rests) of one bar of n32 32nds"""
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
        nxt = positions[i + 1] if i + 1 < len(positions) else n32
        d = pow2(nxt - p)
        add(p, d, events[p])
        cur = p + d
        rests(cur, nxt)
        cur = nxt
    rests(cur, n32)


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


def make_long_song():
    """eight sections of 8 bars, each a dense 4-bar pattern of its own (five instruments on every 16th) with a fill,
    under markers longer than a section's 12-character name: the packed song overflows the sector, so the importer
    must cut patterns, finding each section by index (the names it cut used to miss the markers: a KeyError on
    Pink Floyd's Time)"""
    song = gp.Song()
    song.title = "Long markers"
    song.tempo = 120
    t = gp.Track(song, number=1)
    t.name = "Drums"
    t.isPercussionTrack = True
    t.channel.channel = 9
    t.strings = [gp.GuitarString(i + 1, 0) for i in range(7)]
    t.measures = []
    song.tracks = [t]
    titles = ["Verse 1 ticking away the moments", "Verse 2 kicking around on a piece of ground",
              "Chorus 1 tired of lying in the sunshine", "Guitar solo", "Verse 3 and you run and you run",
              "Bridge one long title", "Bridge two long title", "Last verse long title"]
    song.measureHeaders = []
    for i in range(8 * len(titles)):
        h = gp.MeasureHeader(number=i + 1, start=Q + i * 4 * Q)
        if i % 8 == 0:
            h.marker = gp.Marker(title=titles[i // 8])
        song.measureHeaders.append(h)
        m = gp.Measure(t, h)
        m.voices = [gp.Voice(m)]
        t.measures.append(m)
    for i in range(8 * len(titles)):
        sec, bar = i // 8, i % 4
        e = {}
        for s in range(16):
            for n in (42, 51, 70, 62, 56):                                   # hat, ride, shaker, conga, cowbell
                e.setdefault(s * 2, []).append((n, 63, False))
        for s in (0, 8):
            e.setdefault(s * 2, []).append((36, 95, False))
        e.setdefault(4 * 2, []).append((38, 95, False))
        e.setdefault((12 if bar < 3 else 13) * 2, []).append((38, 95, False))
        e.setdefault((2 + bar) * 2, []).append((36, 95, False))             # each bar of the four its own
        e.setdefault((5 + sec) * 2, []).append((45, 95, False))             # each section its own
        if i % 8 == 7:
            e.setdefault(15 * 2, []).append((50, 95, False))               # the fill: the last bar of the eight only
        emit_bar(t, i, e)
    return song


def main():
    with tempfile.TemporaryDirectory() as d:
        gpf = os.path.join(d, "long.gp4")
        gp.write(make_long_song(), gpf)
        out = os.path.join(d, "long.json")
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "gp2song.py"), gpf, "--out", out],
                           capture_output=True, text=True)
        print(r.stdout.strip())
        assert r.returncode == 0, r.stderr
        with open(out) as f:
            spec = json.load(f)
        names = [s["name"] for s in spec["sections"]]
        assert names == ["VERSE 1 TICK", "VERSE 2 KICK", "CHORUS 1 TIR", "GUITAR SOLO", "VERSE 3 AND ", "BRIDGE ONE L",
                         "BRIDGE TWO L", "LAST VERSE L"], names
        assert "pattern cut to" in r.stdout and len(fm1_song.pack(spec)) <= 3800, r.stdout
        assert any(s["steps"] < 64 for s in spec["sections"]) and any(s["steps"] == 64 for s in spec["sections"]), [s["steps"] for s in spec["sections"]]
        print("gp2song: long markers: the cuts to fit the sector find the sections by index PASS")
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
    assert blob[:4] == b"SNG2" and len(blob) < 3800
    print(f"gp2song: verse (2-bar pattern, fill as lane marks), chorus (accent, flam, tambourine), outro, duplicate skipped, {len(blob)} bytes packed PASS")
    with tempfile.TemporaryDirectory() as d:
        gpf = os.path.join(d, "odd.gp4")
        gp.write(make_odd_song(), gpf)
        out = os.path.join(d, "odd.json")
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "gp2song.py"), gpf, "--out", out],
                           capture_output=True, text=True)
        print(r.stdout.strip())
        assert r.returncode == 0, r.stderr
        with open(out) as f:
            spec = json.load(f)
    seven, four = spec["sections"]
    assert seven["time"] == "7/8" and seven["steps"] == 14 and seven["lanes"]["KICK"] == "x.....x.......", seven
    assert "time" not in four and four["steps"] == 16, four
    blob = fm1_song.pack(spec)
    assert fm1_song.sections(blob)[0][4] == 7 | 1 << 5 and fm1_song.sections(blob)[1][4] == 0
    print("gp2song: a 7/8 section: 14 sixteenths, \"time\": \"7/8\", the meter byte packed; 4/4 left out PASS")
    with tempfile.TemporaryDirectory() as d:
        gpf = os.path.join(d, "many.gp4")
        gp.write(make_many_song(), gpf)
        out = os.path.join(d, "many.json")
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "gp2song.py"), gpf, "--out", out],
                           capture_output=True, text=True)
        assert r.returncode == 0, r.stderr
        with open(out) as f:
            spec = json.load(f)
    names = [s["name"] for s in spec["sections"]]
    assert len(names) == 16 and names[-1] == "OUTRO" and spec["sections"][-1].get("outro") and names[0] == "SECTION 1", names
    assert "more than 16 sections: dropped" in r.stdout, r.stdout
    assert len(fm1_song.pack(spec)) <= 3800
    print("gp2song: eighteen parts: sixteen kept (dropped from the end, the outro stays), packed PASS")


def make_many_song():
    """eighteen sections of a bar each, every one its own pattern (the kick on a 16th of its own, the snare on 4 or
    12), the last called Outro: more than a song holds (16), so the importer drops from the end, the outro kept"""
    song = gp.Song()
    song.title = "Many"
    song.tempo = 120
    t = gp.Track(song, number=1)
    t.name = "Drums"
    t.isPercussionTrack = True
    t.channel.channel = 9
    t.strings = [gp.GuitarString(i + 1, 0) for i in range(6)]
    t.measures = []
    song.tracks = [t]
    song.measureHeaders = []
    for i in range(18):
        h = gp.MeasureHeader(number=i + 1, start=Q + i * 4 * Q)
        h.marker = gp.Marker(title="Outro" if i == 17 else f"Section {i + 1}")
        song.measureHeaders.append(h)
        m = gp.Measure(t, h)
        m.voices = [gp.Voice(m)]
        t.measures.append(m)
    for i in range(18):
        e = {0: [(36, 95, False)]}
        e.setdefault((1 + i % 15) * 2, []).append((36, 95, False))
        e.setdefault((4 if i < 9 else 12) * 2, []).append((38, 95, False))
        emit_bar(t, i, e)
    return song


def make_odd_song():
    """four bars of 7/8 (Verse), then two of 4/4 (Outro)"""
    song = gp.Song()
    song.title = "Odd"
    song.tempo = 120
    t = gp.Track(song, number=1)
    t.name = "Drums"
    t.isPercussionTrack = True
    t.channel.channel = 9
    t.strings = [gp.GuitarString(i + 1, 0) for i in range(6)]
    t.measures = []
    song.tracks = [t]
    song.measureHeaders = []
    start = Q
    for i in range(6):
        odd = i < 4
        h = gp.MeasureHeader(number=i + 1, start=start)
        h.timeSignature = gp.TimeSignature(numerator=7 if odd else 4, denominator=gp.Duration(value=8 if odd else 4))
        if i in (0, 4):
            h.marker = gp.Marker(title="Verse" if odd else "Outro")
        song.measureHeaders.append(h)
        m = gp.Measure(t, h)
        m.voices = [gp.Voice(m)]
        t.measures.append(m)
        start += 7 * Q // 2 if odd else 4 * Q
    for i in range(6):
        e = {}
        if i < 4:
            for s in range(0, 14, 2):
                e.setdefault(s * 2, []).append((42, 95, False))
            for s in (0, 6):
                e.setdefault(s * 2, []).append((36, 95, False))
            e.setdefault(4 * 2, []).append((38, 95, False))
            e.setdefault(10 * 2, []).append((38, 95, False))
            emit_bar(t, i, e, 28)
        else:
            e.setdefault(0, []).append((36, 95, False))
            e.setdefault(0, []).append((49, 95, False))
            emit_bar(t, i, e)
    return song


if __name__ == "__main__":
    main()
