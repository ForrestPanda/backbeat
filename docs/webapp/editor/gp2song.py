#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""fm1-drummer: a song (JSON for tools/fm1_song.py) from the drum track of a Guitar Pro file.

    tools/gp2song.py "Pink Floyd - Comfortably Numb.gp3"               # print what it would make
    tools/gp2song.py song.gp4 --out songs/song.json
    tools/gp2song.py song.gp4 --sections "Intro,Verse 1,Chorus,Outro" --out songs/song.json

Needs PyGuitarPro (pip install pyguitarpro): .gp3, .gp4 and .gp5.

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
(the song has one tempo). The JSON is meant to be edited afterwards."""
import argparse
import collections
import json
import sys

try:
    import guitarpro as gp
except ImportError:
    sys.exit("needs PyGuitarPro: pip install pyguitarpro")

LANES = ["KICK", "KICK2", "SNARE", "CLAP", "HAT", "OPENHAT", "PEDAL", "RIM",
         "SNARE2", "LOWTOM", "MIDTOM", "HITOM", "CRASH", "RIDE", "SHAKER", "COWBELL"]
GM = {35: "KICK", 36: "KICK", 38: "SNARE", 40: "SNARE", 37: "RIM", 31: "RIM", 39: "CLAP",
      42: "HAT", 44: "PEDAL", 46: "OPENHAT",
      41: "LOWTOM", 43: "LOWTOM", 45: "MIDTOM", 47: "MIDTOM", 48: "HITOM", 50: "HITOM",
      49: "CRASH", 52: "CRASH", 55: "CRASH", 57: "CRASH", 51: "RIDE", 53: "RIDE", 59: "RIDE",
      56: "COWBELL", 67: "COWBELL", 68: "COWBELL", 54: "SHAKER", 69: "SHAKER", 70: "SHAKER", 82: "SHAKER",
      65: "HITOM", 66: "MIDTOM",
      75: "RIM", 76: "RIM", 77: "RIM", 58: "RIM", 80: "SHAKER", 81: "SHAKER"}
QUARTER = 960                                 # Duration.quarterTime
LEVEL_CHAR = {0: "x", 1: "g", 2: "s", 3: "X"}  # LV_NORM, LV_GHOST, LV_SOFT, LV_HARD
OUTRO_NAMES = ("OUTRO", "ENDING", "END", "CODA")
MATCH = 0.75                                  # a pattern: this share of the bars agree with it
NEAR = 0.1                                    # a shorter pattern scoring this close to the best is taken
SNG_MAX = 3800                                # the packed song (firmware/src/sng.c SNG_MAX)
SNG_SECTIONS = 16                             # sections a song holds (seq.c SNG_SECTIONS; 8 until 2026-10-14)
METER_DENS = (2, 4, 8, 16)                    # the meters a section can have (sng.c): n 1..16 over these
OFFGRID_32 = 0.125                            # 1/32 when this share of the hits sit between 16ths


def level_of(note):
    e = note.effect
    if e.heavyAccentuatedNote or e.accentuatedNote:
        return 3
    if e.ghostNote:
        return 1
    v = note.velocity
    return 1 if v < 50 else 2 if v < 80 else 0 if v < 110 else 3


def beats_of(measures):
    """the (offset in the bar, note) pairs of one bar (the same measure of every percussion track)"""
    start = measures[0].header.start
    for m in measures:
        for voice in m.voices:
            for beat in voice.beats:
                if beat.status != gp.BeatStatus.normal:
                    continue
                for note in beat.notes:
                    if note.type == gp.NoteType.normal and note.value in GM:
                        yield beat.start - start, note


def bar_hits(measures, ticks_per_step):
    """one bar on a grid: {step: {lane: (level, ratchet, flam)}} (hashable: a sorted tuple), and its step count; a
    flam is the file's grace note on the hit (two notes a 32nd apart are a ratchet, as before)"""
    length = measures[0].header.length
    nsteps = max(1, round(length / ticks_per_step))
    raw = {}
    for pos, note in beats_of(measures):
        step = int(round(pos / ticks_per_step))
        if step >= nsteps:
            continue
        raw.setdefault((step, GM[note.value]), []).append((pos - step * ticks_per_step, level_of(note), note.effect.grace is not None))
    hits = {}
    for (step, lane), lst in raw.items():
        level = max(l for _, l, _ in lst)
        subs = {s for s, _, _ in lst}
        flam = any(f for _, _, f in lst)
        hits.setdefault(step, {})[lane] = (level, min(4, len(subs)) if len(subs) > 1 else 1, flam)
    return nsteps, tuple(sorted((s, tuple(sorted(h.items()))) for s, h in hits.items()))


def offgrid_share(bars):
    total = off = 0
    for measures in bars:
        for pos, _ in beats_of(measures):
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


def make_section(name, bar_measures, outro, max_bars=4):
    """bar_measures: [[measure of each percussion track], ...] for the section's bars"""
    div = "1/32" if offgrid_share(bar_measures) >= OFFGRID_32 else "1/16"
    tps = QUARTER // (8 if div == "1/32" else 4)
    max_bars = min(max_bars, 2 if div == "1/32" else 4)
    bars = [bar_hits(ms, tps) for ms in bar_measures]
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
    ts = next(ms[0].header.timeSignature for ms, b in zip(bar_measures, bars) if b[0] == steps_per_bar)   # the pattern's bars'
    num, den = ts.numerator, ts.denominator.value
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


def auto_sections(song, tracks, min_bars=4):
    """sections found from the drums alone (a file without markers): a run of bars that repeats a pattern of
    1..4 bars (fills allowed) is a section; runs of silence are dropped, short runs join their neighbour"""
    tps = QUARTER // 4
    all_bars = [[t.measures[k] for t in tracks] for k in range(len(song.measureHeaders))]
    keys = [bar_hits(ms, tps)[1] for ms in all_bars]
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


def sections_of(song, tracks):
    """[(name, [[measures of the bar]...], first bar, last bar)] from the markers"""
    heads = song.measureHeaders
    bounds = [(h.number - 1, (h.marker.title or "").strip()) for h in heads if h.marker]
    if not bounds or bounds[0][0] != 0:
        bounds.insert(0, (0, ""))
    out = []
    for i, (start, title) in enumerate(bounds):
        end = bounds[i + 1][0] if i + 1 < len(bounds) else len(heads)
        if end <= start:
            continue
        name = title.upper() if title else f"PART {i + 1}"
        bars = [[t.measures[k] for t in tracks] for k in range(start, end)]
        out.append((name, bars, start + 1, end))
    return out


def bar_strings(measures, tps=QUARTER // 4):
    """one bar as the JSON's strings at 1/16: {"steps": n, "time": "7/8" (not in 4/4), "lanes": {lane: "x..."},
    "ratchets": {lane: "..2."}, "flams": {lane: "f..."}}"""
    n, hits = bar_hits(measures, tps)
    lanes, rats, flams = {}, {}, {}
    for s, h in hits:
        for lane, (lvl, rat, fl) in h:
            lanes.setdefault(lane, ["."] * n)[s] = LEVEL_CHAR[lvl]
            if rat > 1:
                rats.setdefault(lane, ["."] * n)[s] = str(rat)
            if fl:
                flams.setdefault(lane, ["."] * n)[s] = "f"
    out = {"steps": n, "lanes": {l: "".join(v) for l, v in lanes.items()}}
    ts = measures[0].header.timeSignature
    if (ts.numerator, ts.denominator.value) != (4, 4):
        out["time"] = f"{ts.numerator}/{ts.denominator.value}"
    if rats:
        out["ratchets"] = {l: "".join(v) for l, v in rats.items()}
    if flams:
        out["flams"] = {l: "".join(v) for l, v in flams.items()}
    return out


def convert(source, sections=None, track=None, kit=46, keep_duplicates=False, auto=False, log=print, title=None, detail=False):
    """a Guitar Pro file (a path, or a file-like object) -> the song JSON (a dict); what main() prints goes to log.
    sections: marker titles to keep, in order; track: one track's number; auto: sections from the drums alone.
    detail: the dict also carries "bars" (every bar of the file as strings at 1/16) and "ranges" (each section's
    bars, 1-based, as found), for an editor that lets the user pick a pattern's bars by hand."""
    song = gp.parse(source)
    tracks = [t for t in song.tracks if t.number == track] if track else [t for t in song.tracks if t.isPercussionTrack]
    if not tracks:
        raise ValueError("no percussion track in this file (--track n to pick one)")
    name = (song.title or (title or "SONG")).upper()[:12]
    log(f"{song.title or title or 'song'}: {song.tempo} BPM, {len(song.measureHeaders)} measures, percussion: " +
        ", ".join(f"track {t.number} {t.name.strip()!r}" for t in tracks))
    wanted = [s.strip().upper() for s in sections.split(",")] if sections else None
    if auto or not any(h.marker for h in song.measureHeaders):
        secs = auto_sections(song, tracks)
        log(f"  no markers{' used' if auto and any(h.marker for h in song.measureHeaders) else ''}: {len(secs)} sections found from the drums (bars that repeat a pattern); rename them in the JSON")
    else:
        secs = sections_of(song, tracks)
    if wanted:
        by_name = {n: x for x in secs for n in [x[0]]}
        missing = [w for w in wanted if w not in by_name]
        if missing:
            raise ValueError(f"no section {', '.join(missing)}; the file has: {', '.join(n for n, _, _, _ in secs)}")
        secs = [by_name[w] for w in wanted]
    out, src, seen, skipped = [], [], {}, []      # src: each kept section's (bars, outro), for the cuts below
    for sname, bars, first, last in secs:
        outro = any(sname.startswith(w) for w in OUTRO_NAMES)
        sec, p, score, has_fill, dropped = make_section(sname, bars, outro)
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
        log(f"  {sname:12s} bars {first:3d}-{last:3d}: pattern {p} bar{'s' if p > 1 else ''} ({score:.0%} of {len(bars)}), "
            f"{sec['steps']} steps {sec['div']}{' in ' + sec['time'] if 'time' in sec else ''}, {hits} hits"
            f"{', a fill' if has_fill else ''}{', OUTRO' if outro else ''}")
        for step, lanes in dropped:
            log(f"    fill hit dropped at step {step + 1} ({', '.join(lanes)}): the pattern hits there too")
        out.append(sec)
        src.append((bars, outro))
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
        bars, outro = src[i]
        sec, p, score, has_fill, dropped = make_section(out[i]["name"], bars, outro, limits[i])
        out[i] = sec
        log(f"  {out[i]['name']}: pattern cut to {p} bar{'s' if p > 1 else ''} ({score:.0%}) so the song fits its flash sector")
    if not any(s.get("outro") for s in out):
        log("  no section named OUTRO / ENDING / END: the ending stops on the next bar (set \"outro\": true in the JSON)")
    spec = {"name": name, "bpm": int(song.tempo), "kit": kit, "swing": 0, "drum_level": 100, "drum_reverb": 20,
            "sections": out}
    if detail:
        spec["bars"] = [bar_strings([t.measures[k] for t in tracks]) for k in range(len(song.measureHeaders))]
        spec["ranges"] = [{"name": n, "first": first, "last": last} for n, _, first, last in secs]
    return spec


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("gp", help="the .gp3 / .gp4 / .gp5 file")
    ap.add_argument("--out", help="the song JSON to write")
    ap.add_argument("--sections", help="the marker titles to keep, in order, comma separated")
    ap.add_argument("--track", type=int, help="one track's number (default: every percussion track)")
    ap.add_argument("--kit", type=int, default=0, help="the kit index (default 0, MULDJORD: the layered kit; 1 808)")
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
