#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""fm1-drummer: a song (JSON for tools/fm1_song.py) from the drum track of a Guitar Pro file or a MIDI file.

    tools/gp2song.py "Pink Floyd - Comfortably Numb.gp3"               # print what it would make
    tools/gp2song.py song.gp4 --out songs/song.json
    tools/gp2song.py song.mid --out songs/song.json                     # a Standard MIDI File (type 0 or 1)
    tools/gp2song.py song.gp4 --sections "Intro,Verse 1,Chorus,Outro" --out songs/song.json

Guitar Pro files (.gp3, .gp4, .gp5) need PyGuitarPro (pip install pyguitarpro); MIDI files need nothing, the
reader is here (since 2026-10-19). The file's format is told from its first bytes, not its name.

The score's section markers (Intro, Verse 1, Chorus...) bound the sections; the measures of a section give
its drum pattern: the run of 1, 2 or 4 bars that most of the section repeats (at 1/16; at 1/32, and then
at most 2 bars, when many hits sit between 16ths). The bar that deviates from the pattern at the end of
the section, or most often at the end of a phrase, is the fill: the section's fill bar of its own ("fillbar" in
the JSON, since 2026-10-15: that bar as it is, levels, ratchets and flams, which a fill plays instead of the bar it is
asked in; before, its hits were marks on the pattern's last bar, "fills", and a hit on a step the pattern also
hit lost its own level). A grace note on a hit is a flam ("flams", since 2026-10-17); two notes a 32nd apart are a
ratchet. Every percussion track of the file goes into the pattern (drums and percussion). Sections whose
pattern repeats an earlier one (Chorus 2, Chorus 3) are skipped; at most 16 are kept, the outro always
(--sections picks and orders them by their marker titles). A section called OUTRO, ENDING, END or CODA is
the outro. GM drum notes go to the 16 lanes (kick 35 36, snare 38 40, side stick 37, clap 39, hats 42 44
46, toms 41 43 low, 45 47 mid, 48 50 high, crash 49 52 55 57, ride 51 53 59, cowbell 56, shaker and tambourine 54 69 70 82;
the congas and bongos 60..64 are left out: no lane since the mid tom took the conga's); accents and ghost notes become HARD and GHOST hits, velocities otherwise the four levels (under
50 ghost, under 80 soft, under 110 normal, else hard); two 32nd hits in one 16th step become a ratchet.
A section's meter is its pattern bars' time signature ("time": "7/8" in the JSON; 4/4 is left out): the
device counts the pedal's bars, the click and the count-in in it. Tempo changes inside the file are ignored
(the song has one tempo). A triplet feel marked on the score (two 8ths played as a triplet's quarter and 8th: a
shuffle) becomes the song's swing, 67 (2:1), on the 8ths ("swing_grid": "1/8", since 2026-10-19: a 1/16 pattern with its
hats on the 8ths shuffles; swing on the 16ths, the default, would move only the steps between them). The JSON is
meant to be edited afterwards.

A MIDI file goes the same way once read: the drums are the notes on channel 10 of any track (--track n takes
one track's notes whatever their channel; a file without channel 10 notes gives a track named for drums or
percussion, else a track whose notes all sit in the GM drum range with a kick or a snare among them); the tempo
is the first set-tempo event; the bars follow the time signature meta events (4/4 until the first); the
sections are the marker meta events (cue points when there are none), each at the nearest bar line, and without
any the drums alone give them, as --auto does. Velocities give the levels (a MIDI file has no accent flags); two
notes of one drum less than a 32nd apart are a flam (the first is the grace); a note within a 64th triplet of a
32nd line is put on it, so a humanised part lands on the grid, while triplets stay where they are (and count as
hits between 16ths, as in a Guitar Pro file).
"""
import argparse
import bisect
import collections
import io
import json
import struct
import sys

LANES = ["KICK", "KICK2", "SNARE", "CLAP", "HAT", "OPENHAT", "PEDAL", "RIM",
         "SNARE2", "LOWTOM", "MIDTOM", "HITOM", "CRASH", "RIDE", "SHAKER", "COWBELL"]
GM = {35: "KICK", 36: "KICK", 38: "SNARE", 40: "SNARE", 37: "RIM", 31: "RIM", 39: "CLAP",
      42: "HAT", 44: "PEDAL", 46: "OPENHAT",
      41: "LOWTOM", 43: "LOWTOM", 45: "MIDTOM", 47: "MIDTOM", 48: "HITOM", 50: "HITOM",
      49: "CRASH", 52: "CRASH", 55: "CRASH", 57: "CRASH", 51: "RIDE", 53: "RIDE", 59: "RIDE",
      56: "COWBELL", 67: "COWBELL", 68: "COWBELL", 54: "SHAKER", 69: "SHAKER", 70: "SHAKER", 82: "SHAKER",
      65: "HITOM", 66: "MIDTOM",
      75: "RIM", 76: "RIM", 77: "RIM", 58: "RIM", 80: "SHAKER", 81: "SHAKER"}
QUARTER = 960                                 # Duration.quarterTime: every reader counts in these ticks
LEVEL_CHAR = {0: "x", 1: "g", 2: "s", 3: "X"}  # LV_NORM, LV_GHOST, LV_SOFT, LV_HARD
OUTRO_NAMES = ("OUTRO", "ENDING", "END", "CODA")
MATCH = 0.75                                  # a pattern: this share of the bars agree with it
NEAR = 0.1                                    # a shorter pattern scoring this close to the best is taken
SNG_MAX = 3800                                # the packed song (firmware/src/sng.c SNG_MAX)
SNG_SECTIONS = 16                             # sections a song holds (seq.c SNG_SECTIONS; 8 until 2026-10-14)
METER_DENS = (2, 4, 8, 16)                    # the meters a section can have (sng.c): n 1..16 over these
OFFGRID_32 = 0.125                            # 1/32 when this share of the hits sit between 16ths
MIDI_SNAP = QUARTER // 24                     # a MIDI note nearer than this to a 32nd line is put on it (a 64th triplet: 8th
                                              # and 16th triplets, 320 and 160 ticks, keep their place)
MIDI_FLAM = QUARTER // 8                      # two MIDI notes of one drum closer than a 32nd: a flam, the first its grace
MIDI_DRUM_NAMES = ("DRUM", "PERC", "KIT", "BATT", "SCHLAG")   # a track named so holds the drums when no channel 10 does
MIDI_MAX_BARS = 4096                          # a file is not read past this (a tick count gone wild)
KIT_DEFAULT = 0                               # the song's kit: fm1_song.py KIT_DEFAULT, MULDJORD (46 until 2026-10-15: the GM kits went,
                                              # and a song still saying 46 is refused by the device, sng.c sng_read)
SHUFFLE_SWING = 67                            # a Guitar Pro triplet feel as the song's swing: 2:1, MPC 66.7 % (seq.c swing_units)


class Bar:
    """one bar of the file, whichever format it came from: its length in QUARTER ticks, its time signature
    (numerator, denominator) and its notes, (position in the bar, lane, level, flam)"""
    __slots__ = ("length", "ts", "notes")

    def __init__(self, length, ts):
        self.length, self.ts, self.notes = length, ts, []


def level_of_velocity(v):
    return 1 if v < 50 else 2 if v < 80 else 0 if v < 110 else 3


def gp_level(note):
    e = note.effect
    if e.heavyAccentuatedNote or e.accentuatedNote:
        return 3
    if e.ghostNote:
        return 1
    return level_of_velocity(note.velocity)


def read_gp(source, track=None):
    """a Guitar Pro file (a path or a file object) -> (title, tempo, [Bar], [(bar index, marker title)], a line about it,
    the triplet feel: "eighth", "sixteenth" or None)"""
    try:
        import guitarpro as gp
    except ImportError:
        raise ValueError("needs PyGuitarPro: pip install pyguitarpro")
    song = gp.parse(source)
    tracks = [t for t in song.tracks if t.number == track] if track else [t for t in song.tracks if t.isPercussionTrack]
    if not tracks:
        raise ValueError("no percussion track in this file (--track n to pick one)")
    bars = []
    for k, h in enumerate(song.measureHeaders):
        bar = Bar(h.length, (h.timeSignature.numerator, h.timeSignature.denominator.value))
        for t in tracks:
            for voice in t.measures[k].voices:
                for beat in voice.beats:
                    if beat.status != gp.BeatStatus.normal:
                        continue
                    for note in beat.notes:
                        if note.type == gp.NoteType.normal and note.value in GM:
                            bar.notes.append((beat.start - h.start, GM[note.value], gp_level(note), note.effect.grace is not None))
        bars.append(bar)
    markers = [(h.number - 1, (h.marker.title or "").strip()) for h in song.measureHeaders if h.marker]
    info = f"{song.tempo} BPM, {len(bars)} measures, percussion: " + ", ".join(f"track {t.number} {t.name.strip()!r}" for t in tracks)
    feels = collections.Counter(h.tripletFeel.name for h in song.measureHeaders if h.tripletFeel.name != "none")   # the shuffle
    feel = feels.most_common(1)[0][0] if feels and 2 * sum(feels.values()) >= len(bars) else None   # mark: on most measures
    return song.title, int(song.tempo), bars, markers, info, feel


def _vlq(data, i):
    v = 0
    while True:
        b = data[i]
        i += 1
        v = v << 7 | b & 0x7F
        if not b & 0x80:
            return v, i


def midi_chunks(data):
    """a Standard MIDI File -> (format, ticks a quarter, tracks); a track is [(tick, status, data1, data2)] for the
    channel messages and [(tick, 0xFF, type, bytes)] for the meta events, in file order; SysEx is skipped"""
    if data[:4] != b"MThd" or len(data) < 14:
        raise ValueError("not a MIDI file (no MThd header)")
    hlen = struct.unpack(">I", data[4:8])[0]
    fmt, ntrk, div = struct.unpack(">HHH", data[8:14])
    if div & 0x8000:
        raise ValueError("SMPTE time in this MIDI file: not supported")
    i, tracks = 8 + hlen, []
    while i + 8 <= len(data) and len(tracks) < max(1, ntrk):
        tag, ln = data[i:i + 4], struct.unpack(">I", data[i + 4:i + 8])[0]
        chunk, i = data[i + 8:i + 8 + ln], i + 8 + ln
        if tag != b"MTrk":
            continue
        ev, j, tick, status = [], 0, 0, 0
        try:
            while j < len(chunk):
                d, j = _vlq(chunk, j)
                tick += d
                b = chunk[j]
                if b == 0xFF:
                    typ = chunk[j + 1]
                    ln, j = _vlq(chunk, j + 2)
                    ev.append((tick, 0xFF, typ, bytes(chunk[j:j + ln])))
                    j += ln
                    if typ == 0x2F:
                        break
                elif b in (0xF0, 0xF7):
                    ln, j = _vlq(chunk, j + 1)
                    j += ln
                else:
                    if b & 0x80:
                        status, j = b, j + 1
                    elif not status:
                        raise ValueError(f"MIDI track {len(tracks) + 1}: a data byte before any status")
                    n = 1 if (status & 0xF0) in (0xC0, 0xD0) else 2
                    ev.append((tick, status, chunk[j], chunk[j + 1] if n == 2 else 0))
                    j += n
        except IndexError:
            raise ValueError(f"MIDI track {len(tracks) + 1} ends in the middle of an event")
        tracks.append(ev)
    if not tracks:
        raise ValueError("no track in this MIDI file")
    return fmt, div or 480, tracks


def read_midi(data, track=None, title=None):
    """a Standard MIDI File (bytes) -> (title, tempo, [Bar], [(bar index, marker title)], a line about it): the drums
    as the module's docstring says, the notes in QUARTER ticks, flams found, humanised notes snapped to the 32nds"""
    fmt, tpq, tracks = midi_chunks(data)
    text = lambda b: b.decode("utf-8", "replace").strip()
    names = [next((text(e[3]) for e in ev if e[1] == 0xFF and e[2] == 0x03), "") for ev in tracks]

    def notes_of(ev, chan=None):
        return [(t, d1, d2) for t, st, d1, d2 in ev if st & 0xF0 == 0x90 and d2 > 0 and (chan is None or st & 0x0F == chan)]

    if track:
        if not 1 <= track <= len(tracks):
            raise ValueError(f"no track {track}: the file has {len(tracks)}")
        picked, how = [(track, notes_of(tracks[track - 1]))], "all channels of the track"
    else:
        picked, how = [(k + 1, notes_of(ev, 9)) for k, ev in enumerate(tracks)], "channel 10"
        picked = [p for p in picked if p[1]]
        if not picked:
            picked = [(k + 1, notes_of(ev)) for k, ev in enumerate(tracks) if any(w in names[k].upper() for w in MIDI_DRUM_NAMES)]
            picked, how = [p for p in picked if p[1]], "the track named for drums"
        if not picked:
            for k, ev in enumerate(tracks):
                ns = notes_of(ev)
                if ns and all(35 <= n <= 81 for _, n, _ in ns) and any(n in (35, 36, 38, 40) for _, n, _ in ns):
                    picked, how = [(k + 1, ns)], "the track whose notes all sit in the GM drum range"
                    break
        if not picked:
            raise ValueError("no drum notes (channel 10) in this MIDI file; its tracks: "
                             + ", ".join(f"{k + 1} {n!r}" for k, n in enumerate(names)) + " (--track n to pick one)")
    scale = QUARTER / tpq
    metas = sorted((e for ev in tracks for e in ev if e[1] == 0xFF), key=lambda e: e[0])
    tempos = [e for e in metas if e[2] == 0x51 and len(e[3]) == 3]
    bpm = round(60e6 / max(1, int.from_bytes(tempos[0][3], "big"))) if tempos else 120
    sigs = sorted((round(e[0] * scale), e[3][0], 1 << e[3][1]) for e in metas if e[2] == 0x58 and len(e[3]) >= 2 and e[3][0])
    marks = [(round(e[0] * scale), text(e[3])) for e in metas if e[2] == 0x06]
    marks = marks or [(round(e[0] * scale), text(e[3])) for e in metas if e[2] == 0x07]
    # the notes by lane: a note less than a 32nd after another of the same drum is a flam on it (the first its grace,
    # the hit keeps its own velocity); two at one tick are one note; then each is snapped to a 32nd line it is near
    by_lane = collections.defaultdict(list)
    for _, ns in picked:
        for t, n, v in ns:
            if n in GM:
                by_lane[GM[n]].append((round(t * scale), v))
    notes = []                                                       # (tick, lane, level, flam)
    for lane, lst in by_lane.items():
        lst.sort()
        i = 0
        while i < len(lst):
            t, v, flam = lst[i][0], lst[i][1], False
            while i + 1 < len(lst) and lst[i + 1][0] - t < MIDI_FLAM:
                t2, v2 = lst[i + 1]
                if t2 == t:
                    v = max(v, v2)
                else:
                    t, v, flam = t2, v2, True
                i += 1
            off = t % (QUARTER // 8)
            if off < MIDI_SNAP:
                t -= off
            elif off > QUARTER // 8 - MIDI_SNAP:
                t += QUARTER // 8 - off
            notes.append((t, lane, level_of_velocity(v), flam))
            i += 1
    # the bars: the time signatures in force (a change takes the bar line it sits on, or the next)
    end = max([t for t, _, _, _ in notes] + [s[0] for s in sigs] + [m[0] for m in marks] + [0]) + 1
    bars, starts, start, si, ts = [], [], 0, 0, (4, 4)
    while start < end and len(bars) < MIDI_MAX_BARS:
        while si < len(sigs) and sigs[si][0] <= start + MIDI_SNAP:
            ts, si = sigs[si][1:], si + 1
        length = QUARTER * 4 * ts[0] // ts[1]
        bars.append(Bar(length, ts))
        starts.append(start)
        start += length
    for t, lane, level, flam in sorted(notes):
        k = bisect.bisect_right(starts, t) - 1
        if 0 <= k < len(bars):
            bars[k].notes.append((t - starts[k], lane, level, flam))
    markers, seen = [], set()
    for t, name in marks:
        k = bisect.bisect_right(starts, t) - 1
        if k < 0 or k >= len(bars):
            continue
        if t - starts[k] > bars[k].length // 2 and k + 1 < len(bars):
            k += 1
        if k not in seen:
            seen.add(k)
            markers.append((k, name))
    name0 = names[0] if names[0] and not any(w in names[0].upper() for w in MIDI_DRUM_NAMES) else ""
    info = (f"{bpm} BPM, {len(bars)} bars, type {fmt}, drums: {how}: "
            + ", ".join(f"track {k} {names[k - 1]!r} ({len(ns)} notes)" for k, ns in picked)
            + (f"; {len(tempos) - 1} tempo change{'s' if len(tempos) > 2 else ''} ignored" if len(tempos) > 1 else ""))
    return name0 or title, bpm, bars, sorted(markers), info, None   # (no triplet feel in a MIDI file)


def bar_hits(bar, ticks_per_step):
    """one bar on a grid: {step: {lane: (level, ratchet, flam)}} (hashable: a sorted tuple), and its step count; a
    flam is the file's grace note on the hit (two notes a 32nd apart are a ratchet, as before)"""
    nsteps = max(1, round(bar.length / ticks_per_step))
    raw = {}
    for pos, lane, level, flam in bar.notes:
        step = int(round(pos / ticks_per_step))
        if step >= nsteps:
            continue
        raw.setdefault((step, lane), []).append((pos - step * ticks_per_step, level, flam))
    hits = {}
    for (step, lane), lst in raw.items():
        level = max(l for _, l, _ in lst)
        subs = {s for s, _, _ in lst}
        flam = any(f for _, _, f in lst)
        hits.setdefault(step, {})[lane] = (level, min(4, len(subs)) if len(subs) > 1 else 1, flam)
    return nsteps, tuple(sorted((s, tuple(sorted(h.items()))) for s, h in hits.items()))


def offgrid_share(bars):
    total = off = 0
    for bar in bars:
        for pos, _, _, _ in bar.notes:
            total += 1
            off += pos % (QUARTER // 4) != 0
    return off / total if total else 0.0


def best_period(bars, max_p):
    """(p, pattern, score): the run of 1, 2, 3 or 4 bars (the most common bar at each position) most of the
    section repeats: the shortest that matches, else the shortest whose score is within NEAR of the best"""
    found = []
    for p in (1, 2, 3, 4):
        if p > max_p or p > len(bars):
            continue
        pattern = []
        for i in range(p):
            c = collections.Counter(bars[j] for j in range(i, len(bars), p))
            pattern.append(c.most_common(1)[0][0])
        score = sum(bars[i] == pattern[i % p] for i in range(len(bars))) / len(bars)
        if score >= MATCH:
            return p, pattern, score
        found.append((p, pattern, score))
    top = max(f[2] for f in found)
    return next(f for f in found if f[2] >= top - NEAR)


def make_section(name, bar_list, outro, max_bars=4):
    """bar_list: the section's Bars"""
    div = "1/32" if offgrid_share(bar_list) >= OFFGRID_32 else "1/16"
    tps = QUARTER // (8 if div == "1/32" else 4)
    max_bars = min(max_bars, 2 if div == "1/32" else 4)
    bars = [bar_hits(b, tps) for b in bar_list]
    p, pattern, score = best_period(bars, max_bars)
    # the fill: the last bar if it deviates; else the most common deviating bar at the end of a phrase
    fill = None
    if len(bars) > p and bars[-1] != pattern[(len(bars) - 1) % p]:
        fill = bars[-1]
    elif score >= MATCH:                           # (a section full of variations has no one fill)
        c = collections.Counter(bars[i] for i in range(p - 1, len(bars), p) if bars[i] != pattern[i % p])
        if c:
            fill = c.most_common(1)[0][0]
    steps_per_bar = pattern[0][0]
    steps = steps_per_bar * p
    lanes = {l: ["."] * steps for l in LANES}
    rats = {l: ["."] * steps for l in LANES}
    flams = {l: ["."] * steps for l in LANES}
    for b, (n, hits) in enumerate(pattern):
        for s, h in hits:
            for lane, (lvl, rat, fl) in h:
                lanes[lane][b * steps_per_bar + s] = LEVEL_CHAR[lvl]
                if rat > 1:
                    rats[lane][b * steps_per_bar + s] = str(rat)
                if fl:
                    flams[lane][b * steps_per_bar + s] = "f"
    dropped = []
    fillbar = None
    if fill:                                       # the fill bar of its own: that bar as it is (fill[0] is its steps)
        fl = {l: ["."] * steps_per_bar for l in LANES}
        fr = {l: ["."] * steps_per_bar for l in LANES}
        ff = {l: ["."] * steps_per_bar for l in LANES}
        for s, h in fill[1]:
            if s >= steps_per_bar:
                continue
            for lane, (lvl, rat, flm) in h:
                fl[lane][s] = LEVEL_CHAR[lvl]
                if rat > 1:
                    fr[lane][s] = str(rat)
                if flm:
                    ff[lane][s] = "f"
        fillbar = {"steps": steps_per_bar, "lanes": {l: "".join(v) for l, v in fl.items() if set(v) != {"."}}}
        r = {l: "".join(v) for l, v in fr.items() if set(v) != {"."}}
        if r:
            fillbar["ratchets"] = r
        r = {l: "".join(v) for l, v in ff.items() if set(v) != {"."}}
        if r:
            fillbar["flams"] = r
    sec = {"name": name[:12], "steps": steps, "div": div}
    num, den = next(b.ts for b, h in zip(bar_list, bars) if h[0] == steps_per_bar)   # the pattern's bars' signature
    if (num, den) != (4, 4) and 1 <= num <= 16 and den in METER_DENS:
        sec["time"] = f"{num}/{den}"
    if outro:
        sec["outro"] = True
    sec["lanes"] = {l: "".join(v) for l, v in lanes.items() if set(v) != {"."}}
    r = {l: "".join(v) for l, v in rats.items() if set(v) != {"."}}
    if r:
        sec["ratchets"] = r
    r = {l: "".join(v) for l, v in flams.items() if set(v) != {"."}}
    if r:
        sec["flams"] = r
    if fillbar:
        sec["fillbar"] = fillbar
    return sec, p, score, fill is not None, dropped


def body_key(sec):
    """the pattern without its fill: a later section with the same body is the same section"""
    cond = sec.get("fill", "")
    marks = sec.get("fills", {})
    def strip(l, v):
        m = marks.get(l, "")
        return "".join("." if (i < len(cond) and cond[i] == "F") or (i < len(m) and m[i] == "F") else ch for i, ch in enumerate(v))
    return json.dumps({"steps": sec["steps"], "div": sec["div"],
                       "lanes": {l: strip(l, v) for l, v in sec["lanes"].items() if set(strip(l, v)) != {"."}},
                       "ratchets": {l: strip(l, v) for l, v in sec.get("ratchets", {}).items() if set(strip(l, v)) != {"."}},
                       "flams": {l: strip(l, v) for l, v in sec.get("flams", {}).items() if set(strip(l, v)) != {"."}}},
                      sort_keys=True)


def auto_sections(all_bars, min_bars=4):
    """sections found from the drums alone (a file without markers): a run of bars that repeats a pattern of
    1..4 bars (fills allowed) is a section; runs of silence are dropped, short runs join their neighbour"""
    tps = QUARTER // 4
    keys = [bar_hits(b, tps)[1] for b in all_bars]
    n = len(keys)
    segs = []                                      # [start, end) of the explained runs
    i = 0
    while i < n:
        best = None
        for p in (1, 2, 3, 4):
            if i + p > n:
                break
            pat = keys[i:i + p]
            j, miss = i + p, 0
            while j < n:
                if keys[j] == pat[(j - i) % p]:
                    miss = 0
                elif miss == 0 and j + 1 < n and keys[j + 1] == pat[(j + 1 - i) % p]:
                    miss = 1                       # one bar off the pattern, back on it after: a fill
                else:
                    break
                j += 1
            run = j - i
            if run >= 2 * p and (best is None or run > best[1] + (best[0] - p)):   # longer wins; a tie, the shorter period
                best = (p, run)
        if best is None:
            segs.append([i, i + 1, 0])             # a bar on its own (joined to a neighbour below)
            i += 1
        else:
            segs.append([i, i + best[1], best[0]])
            i += best[1]
    # lone bars and short runs join the run before them (or after, at the start)
    out = []
    for sg in segs:
        if out and (sg[1] - sg[0] < min_bars):
            out[-1][1] = sg[1]
        elif not out and sg[1] - sg[0] < min_bars:
            out.append(sg)
        else:
            if out and out[-1][1] - out[-1][0] < min_bars:
                out[-1][1] = sg[1]
                out[-1][2] = sg[2]
            else:
                out.append(sg)
    # silence dropped; neighbours with the same pattern merged
    def silent(a, b):
        return all(not keys[k] for k in range(a, b))
    res = []
    for a, b, p in out:
        if silent(a, b):
            continue
        if res and p and res[-1][2] == p and keys[res[-1][0]:res[-1][0] + p] == keys[a:a + p]:
            res[-1][1] = b
        else:
            res.append([a, b, p])
    return [(f"PART {k + 1}", all_bars[a:b], a + 1, b) for k, (a, b, p) in enumerate(res)]


def sections_of(all_bars, markers):
    """[(name, [Bar...], first bar, last bar)] from the markers, [(bar index, title)]"""
    bounds = sorted(markers)
    if not bounds or bounds[0][0] != 0:
        bounds.insert(0, (0, ""))
    out = []
    for i, (start, title) in enumerate(bounds):
        end = bounds[i + 1][0] if i + 1 < len(bounds) else len(all_bars)
        if end <= start:
            continue
        name = title.upper() if title else f"PART {i + 1}"
        out.append((name, all_bars[start:end], start + 1, end))
    return out


def bar_strings(bar, tps=QUARTER // 4):
    """one bar as the JSON's strings at 1/16: {"steps": n, "time": "7/8" (not in 4/4), "lanes": {lane: "x..."},
    "ratchets": {lane: "..2."}, "flams": {lane: "f..."}}"""
    n, hits = bar_hits(bar, tps)
    lanes, rats, flams = {}, {}, {}
    for s, h in hits:
        for lane, (lvl, rat, fl) in h:
            lanes.setdefault(lane, ["."] * n)[s] = LEVEL_CHAR[lvl]
            if rat > 1:
                rats.setdefault(lane, ["."] * n)[s] = str(rat)
            if fl:
                flams.setdefault(lane, ["."] * n)[s] = "f"
    out = {"steps": n, "lanes": {l: "".join(v) for l, v in lanes.items()}}
    if bar.ts != (4, 4):
        out["time"] = f"{bar.ts[0]}/{bar.ts[1]}"
    if rats:
        out["ratchets"] = {l: "".join(v) for l, v in rats.items()}
    if flams:
        out["flams"] = {l: "".join(v) for l, v in flams.items()}
    return out


def read_file(source, track=None, title=None):
    """a Guitar Pro or MIDI file (a path, bytes, or a file object), told apart by its first bytes"""
    if isinstance(source, (bytes, bytearray)):
        data = bytes(source)
    elif hasattr(source, "read"):
        data = source.read()
    else:
        with open(source, "rb") as f:
            data = f.read()
    if data[:4] == b"MThd":
        return read_midi(data, track=track, title=title)
    return read_gp(io.BytesIO(data), track=track)


def convert(source, sections=None, track=None, kit=KIT_DEFAULT, keep_duplicates=False, auto=False, log=print, title=None, detail=False):
    """a Guitar Pro or MIDI file (a path, bytes, or a file-like object) -> the song JSON (a dict); what main() prints
    goes to log. sections: marker titles to keep, in order; track: one track's number; auto: sections from the drums
    alone. detail: the dict also carries "bars" (every bar of the file as strings at 1/16) and "ranges" (each section's
    bars, 1-based, as found), for an editor that lets the user pick a pattern's bars by hand."""
    ftitle, tempo, bars, markers, info, feel = read_file(source, track=track, title=title)
    name = (ftitle or title or "SONG").upper()[:12]
    log(f"{ftitle or title or 'song'}: {info}")
    wanted = [s.strip().upper() for s in sections.split(",")] if sections else None
    if auto or not markers:
        secs = auto_sections(bars)
        log(f"  no markers{' used' if auto and markers else ''}: {len(secs)} sections found from the drums (bars that repeat a pattern); rename them in the JSON")
    else:
        secs = sections_of(bars, markers)
    if wanted:
        by_name = {n: x for x in secs for n in [x[0]]}
        missing = [w for w in wanted if w not in by_name]
        if missing:
            raise ValueError(f"no section {', '.join(missing)}; the file has: {', '.join(n for n, _, _, _ in secs)}")
        secs = [by_name[w] for w in wanted]
    out, src, seen, skipped = [], [], {}, []      # src: each kept section's (bars, outro), for the cuts below
    for sname, sbars, first, last in secs:
        outro = any(sname.startswith(w) for w in OUTRO_NAMES)
        sec, p, score, has_fill, dropped = make_section(sname, sbars, outro)
        key = body_key(sec)
        if key in seen and not keep_duplicates and not wanted and not outro:   # (the outro stays: the ending needs it)
            kept = out[seen[key]]
            if any(k in sec for k in ("fill", "fills", "fillbar")) and not any(k in kept for k in ("fill", "fills", "fillbar")):   # the same body, and this one has the fill: take it
                for k in ("lanes", "ratchets", "flams", "fill", "fills", "fillbar"):
                    if k in sec:
                        kept[k] = sec[k]
                skipped.append(f"{sname} (bars {first}-{last}: as {kept['name']}, whose fill it gives)")
            else:
                skipped.append(f"{sname} (bars {first}-{last}: as {kept['name']})")
            continue
        seen[key] = len(out)
        hits = sum(c != "." for v in sec["lanes"].values() for c in v)
        log(f"  {sname:12s} bars {first:3d}-{last:3d}: pattern {p} bar{'s' if p > 1 else ''} ({score:.0%} of {len(sbars)}), "
            f"{sec['steps']} steps {sec['div']}{' in ' + sec['time'] if 'time' in sec else ''}, {hits} hits"
            f"{', a fill' if has_fill else ''}{', OUTRO' if outro else ''}")
        for step, lanes in dropped:
            log(f"    fill hit dropped at step {step + 1} ({', '.join(lanes)}): the pattern hits there too")
        out.append(sec)
        src.append((sbars, outro))
    for sk in skipped:
        log(f"  skipped {sk}")
    if len(out) > SNG_SECTIONS:                    # unnamed parts go first, then from the end; the outro stays
        order = sorted(range(len(out)), key=lambda i: (bool(out[i].get("outro")), not out[i]["name"].startswith("PART "), -i))
        drop = set(order[:len(out) - SNG_SECTIONS])
        log(f"  more than {SNG_SECTIONS} sections: dropped " + ", ".join(out[i]["name"] for i in sorted(drop)) + " (--sections picks)")
        out = [s for i, s in enumerate(out) if i not in drop]
        src = [s for i, s in enumerate(src) if i not in drop]
    # the packed song must fit its flash sector: the longest patterns give way (4 bars -> 2 -> 1). Sections are
    # found by index: their names are the markers cut to 12 characters, so two may share one
    def packed_size(secs_):                        # as tools/fm1_song.py packs it: 2 bytes a step and a byte a hit (the fill
        return 32 + sum(18 + (x["steps"] + 3) // 4 + 2 * x["steps"] + sum(c != "." for v in x["lanes"].values() for c in v)   # bar's too)
                        + (1 + 2 * x["fillbar"]["steps"] + sum(c != "." for v in x["fillbar"]["lanes"].values() for c in v) if "fillbar" in x else 0)
                        for x in secs_)
    limits = [4] * len(out)
    while packed_size(out) > SNG_MAX:
        i = max(range(len(out)), key=lambda k: (out[k]["steps"], limits[k]))
        if limits[i] <= 1:
            log(f"  the song does not fit ({packed_size(out)} B of {SNG_MAX}): drop sections (--sections)")
            break
        limits[i] //= 2
        sbars, outro = src[i]
        sec, p, score, has_fill, dropped = make_section(out[i]["name"], sbars, outro, limits[i])
        out[i] = sec
        log(f"  {out[i]['name']}: pattern cut to {p} bar{'s' if p > 1 else ''} ({score:.0%}) so the song fits its flash sector")
    if not any(s.get("outro") for s in out):
        log("  no section named OUTRO / ENDING / END: the ending stops on the next bar (set \"outro\": true in the JSON)")
    spec = {"name": name, "bpm": tempo, "kit": kit, "swing": 0, "drum_level": 100, "drum_reverb": 20,
            "sections": out}
    if feel:                                       # the score's triplet feel: the song's swing, on the 8ths for a shuffle
        spec["swing"] = SHUFFLE_SWING
        if feel == "eighth":
            spec["swing_grid"] = "1/8"
        keys = '"swing", "swing_grid": "1/8"' if feel == "eighth" else '"swing"'
        log(f"  triplet feel ({feel}s) in the score: swing {SHUFFLE_SWING} on the {'8ths' if feel == 'eighth' else '16ths'} ({keys} in the JSON)")
    if detail:
        spec["bars"] = [bar_strings(b) for b in bars]
        spec["ranges"] = [{"name": n, "first": first, "last": last} for n, _, first, last in secs]
    return spec


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("gp", metavar="FILE", help="the .gp3 / .gp4 / .gp5 file, or a .mid")
    ap.add_argument("--out", help="the song JSON to write")
    ap.add_argument("--sections", help="the marker titles to keep, in order, comma separated")
    ap.add_argument("--track", type=int, help="one track's number (default: every percussion track; in a MIDI file, every track's channel 10)")
    ap.add_argument("--kit", type=int, default=KIT_DEFAULT, help="the kit index (default 0, MULDJORD: the layered kit; 1 808)")
    ap.add_argument("--keep-duplicates", action="store_true", help="keep sections whose pattern repeats an earlier one")
    ap.add_argument("--auto", action="store_true", help="find the sections from the drums alone, ignoring the markers")
    a = ap.parse_args()
    try:
        spec = convert(a.gp, sections=a.sections, track=a.track, kit=a.kit, keep_duplicates=a.keep_duplicates, auto=a.auto,
                       title=a.gp.rsplit("/", 1)[-1].rsplit(".", 1)[0])
    except ValueError as e:
        sys.exit(str(e))
    out = spec["sections"]
    if a.out:
        with open(a.out, "w") as f:
            json.dump(spec, f, indent=1)
        print(f"wrote {a.out}: {len(out)} sections")


if __name__ == "__main__":
    main()
