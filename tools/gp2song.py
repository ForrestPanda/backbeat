#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""fm1-arranger: a song (JSON for tools/fm1_song.py) from the drum track of a Guitar Pro file.

    tools/gp2song.py "Pink Floyd - Comfortably Numb.gp3"               # print what it would make
    tools/gp2song.py song.gp4 --out songs/song.json
    tools/gp2song.py song.gp4 --sections "Intro,Verse 1,Chorus,Outro" --out songs/song.json

Needs PyGuitarPro (pip install pyguitarpro): .gp3, .gp4 and .gp5.

The score's section markers (Intro, Verse 1, Chorus...) bound the sections; the measures of a section give
its drum pattern: the run of 1, 2 or 4 bars that most of the section repeats (at 1/16; at 1/32, and then
at most 2 bars, when many hits sit between 16ths). The bar that deviates from the pattern at the end of
the section, or most often at the end of a phrase, is the fill: its hits on steps the pattern leaves
silent become FILL ONLY steps, the pattern's hits on steps the fill leaves silent become NO FILL steps
(a step holds one condition for all its lanes, so fill hits that land on a pattern hit are dropped, and
listed). Every percussion track of the file goes into the pattern (drums and percussion). Sections whose
pattern repeats an earlier one (Chorus 2, Chorus 3) are skipped; at most 8 are kept, the outro always
(--sections picks and orders them by their marker titles). A section called OUTRO, ENDING, END or CODA is
the outro. GM drum notes go to the 16 lanes (kick 35 36, snare 38 40, side stick 37, clap 39, hats 42 44
46, toms 41..50, crash 49 52 55 57, ride 51 53 59, cowbell 56, shaker and tambourine 54 69 70 82, congas
60..64); accents and ghost notes become HARD and GHOST hits, velocities otherwise the four levels (under
50 ghost, under 80 soft, under 110 normal, else hard); two 32nd hits in one 16th step become a ratchet.
Tempo changes inside the file are ignored (the song has one tempo). The JSON is meant to be edited
afterwards."""
import argparse
import collections
import json
import sys

try:
    import guitarpro as gp
except ImportError:
    sys.exit("needs PyGuitarPro: pip install pyguitarpro")

LANES = ["KICK", "KICK2", "SNARE", "CLAP", "HAT", "OPENHAT", "PEDAL", "RIM",
         "SNARE2", "LOWTOM", "HITOM", "CRASH", "RIDE", "SHAKER", "CONGA", "COWBELL"]
GM = {35: "KICK", 36: "KICK", 38: "SNARE", 40: "SNARE", 37: "RIM", 31: "RIM", 39: "CLAP",
      42: "HAT", 44: "PEDAL", 46: "OPENHAT",
      41: "LOWTOM", 43: "LOWTOM", 45: "LOWTOM", 47: "HITOM", 48: "HITOM", 50: "HITOM",
      49: "CRASH", 52: "CRASH", 55: "CRASH", 57: "CRASH", 51: "RIDE", 53: "RIDE", 59: "RIDE",
      56: "COWBELL", 67: "COWBELL", 68: "COWBELL", 54: "SHAKER", 69: "SHAKER", 70: "SHAKER", 82: "SHAKER",
      60: "CONGA", 61: "CONGA", 62: "CONGA", 63: "CONGA", 64: "CONGA", 65: "HITOM", 66: "LOWTOM",
      75: "RIM", 76: "RIM", 77: "RIM", 58: "RIM", 80: "SHAKER", 81: "SHAKER"}
QUARTER = 960                                 # Duration.quarterTime
LEVEL_CHAR = {0: "x", 1: "g", 2: "o", 3: "X"}  # LV_NORM, LV_GHOST, LV_SOFT, LV_HARD
OUTRO_NAMES = ("OUTRO", "ENDING", "END", "CODA")
MATCH = 0.75                                  # a pattern: this share of the bars agree with it
NEAR = 0.1                                    # a shorter pattern scoring this close to the best is taken
SNG_MAX = 3800                                # the packed song (firmware/src/sng.c SNG_MAX)
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
    """one bar on a grid: {step: {lane: (level, ratchet)}} (hashable: a sorted tuple), and its step count"""
    length = measures[0].header.length
    nsteps = max(1, round(length / ticks_per_step))
    raw = {}
    for pos, note in beats_of(measures):
        step = int(round(pos / ticks_per_step))
        if step >= nsteps:
            continue
        raw.setdefault((step, GM[note.value]), []).append((pos - step * ticks_per_step, level_of(note)))
    hits = {}
    for (step, lane), lst in raw.items():
        level = max(l for _, l in lst)
        subs = {s for s, _ in lst}
        hits.setdefault(step, {})[lane] = (level, min(4, len(subs)) if len(subs) > 1 else 1)
    return nsteps, tuple(sorted((s, tuple(sorted(h.items()))) for s, h in hits.items()))


def offgrid_share(bars):
    total = off = 0
    for measures in bars:
        for pos, _ in beats_of(measures):
            total += 1
            off += pos % (QUARTER // 4) != 0
    return off / total if total else 0.0


def best_period(bars, max_p):
    """(p, pattern, score): the run of 1, 2 or 4 bars (the most common bar at each position) most of the
    section repeats: the shortest that matches, else the shortest whose score is within NEAR of the best"""
    found = []
    for p in (1, 2, 4):
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
    cond = ["."] * steps
    for b, (n, hits) in enumerate(pattern):
        for s, h in hits:
            for lane, (lvl, rat) in h:
                lanes[lane][b * steps_per_bar + s] = LEVEL_CHAR[lvl]
                if rat > 1:
                    rats[lane][b * steps_per_bar + s] = str(rat)
    dropped = []
    if fill:
        last = (p - 1) * steps_per_bar
        body_last = dict(pattern[p - 1][1])
        fill_hits = dict(fill[1])
        for s in range(steps_per_bar):
            in_body, in_fill = s in body_last, s in fill_hits
            if in_fill and not in_body:
                cond[last + s] = "F"
                for lane, (lvl, rat) in fill_hits[s]:
                    lanes[lane][last + s] = LEVEL_CHAR[lvl]
                    if rat > 1:
                        rats[lane][last + s] = str(rat)
            elif in_body and not in_fill:
                cond[last + s] = "N"
            elif in_body and in_fill and dict(fill_hits[s]) != dict(body_last[s]):
                extra = [l for l, _ in fill_hits[s] if l not in dict(body_last[s])]
                if extra:
                    dropped.append((s, extra))
    sec = {"name": name[:12], "steps": steps, "div": div}
    if outro:
        sec["outro"] = True
    sec["lanes"] = {l: "".join(v) for l, v in lanes.items() if set(v) != {"."}}
    r = {l: "".join(v) for l, v in rats.items() if set(v) != {"."}}
    if r:
        sec["ratchets"] = r
    if fill:
        sec["fill"] = "".join(cond)
    return sec, p, score, fill is not None, dropped


def body_key(sec):
    """the pattern without its fill: a later section with the same body is the same section"""
    cond = sec.get("fill", "")
    strip = lambda v: "".join("." if i < len(cond) and cond[i] == "F" else ch for i, ch in enumerate(v))
    return json.dumps({"steps": sec["steps"], "div": sec["div"],
                       "lanes": {l: strip(v) for l, v in sec["lanes"].items() if set(strip(v)) != {"."}},
                       "ratchets": {l: strip(v) for l, v in sec.get("ratchets", {}).items() if set(strip(v)) != {"."}}},
                      sort_keys=True)


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


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("gp", help="the .gp3 / .gp4 / .gp5 file")
    ap.add_argument("--out", help="the song JSON to write")
    ap.add_argument("--sections", help="the marker titles to keep, in order, comma separated")
    ap.add_argument("--track", type=int, help="one track's number (default: every percussion track)")
    ap.add_argument("--kit", type=int, default=0, help="the kit index (default 0, ACOUSTIC)")
    ap.add_argument("--keep-duplicates", action="store_true", help="keep sections whose pattern repeats an earlier one")
    a = ap.parse_args()
    song = gp.parse(a.gp)
    tracks = [t for t in song.tracks if t.number == a.track] if a.track else [t for t in song.tracks if t.isPercussionTrack]
    if not tracks:
        sys.exit("no percussion track in this file (--track n to pick one)")
    title = (song.title or a.gp.rsplit("/", 1)[-1].rsplit(".", 1)[0]).upper()[:12]
    print(f"{song.title or a.gp}: {song.tempo} BPM, {len(song.measureHeaders)} measures, percussion: " +
          ", ".join(f"track {t.number} {t.name.strip()!r}" for t in tracks))
    wanted = [s.strip().upper() for s in a.sections.split(",")] if a.sections else None
    secs = sections_of(song, tracks)
    if wanted:
        by_name = {n: x for x in secs for n in [x[0]]}
        missing = [w for w in wanted if w not in by_name]
        if missing:
            sys.exit(f"no section {', '.join(missing)}; the file has: {', '.join(n for n, _, _, _ in secs)}")
        secs = [by_name[w] for w in wanted]
    out, seen, skipped = [], {}, []
    for name, bars, first, last in secs:
        outro = any(name.startswith(w) for w in OUTRO_NAMES)
        sec, p, score, has_fill, dropped = make_section(name, bars, outro)
        key = body_key(sec)
        if key in seen and not a.keep_duplicates and not wanted and not outro:   # (the outro stays: the ending needs it)
            kept = out[seen[key]]
            if "fill" in sec and "fill" not in kept:   # the same body, and this one has the fill: take it
                for k in ("lanes", "ratchets", "fill"):
                    if k in sec:
                        kept[k] = sec[k]
                skipped.append(f"{name} (bars {first}-{last}: as {kept['name']}, whose fill it gives)")
            else:
                skipped.append(f"{name} (bars {first}-{last}: as {kept['name']})")
            continue
        seen[key] = len(out)
        hits = sum(c != "." for v in sec["lanes"].values() for c in v)
        print(f"  {name:12s} bars {first:3d}-{last:3d}: pattern {p} bar{'s' if p > 1 else ''} ({score:.0%} of {len(bars)}), "
              f"{sec['steps']} steps {sec['div']}, {hits} hits{', a fill' if has_fill else ''}{', OUTRO' if outro else ''}")
        for step, lanes in dropped:
            print(f"    fill hit dropped at step {step + 1} ({', '.join(lanes)}): the pattern hits there too")
        out.append(sec)
    for sk in skipped:
        print(f"  skipped {sk}")
    if len(out) > 8:                               # unnamed parts go first, then from the end; the outro stays
        order = sorted(range(len(out)), key=lambda i: (bool(out[i].get("outro")), not out[i]["name"].startswith("PART "), -i))
        drop = set(order[:len(out) - 8])
        print("  more than 8 sections: dropped " + ", ".join(out[i]["name"] for i in sorted(drop)) + " (--sections picks)")
        out = [s for i, s in enumerate(out) if i not in drop]
    # the packed song must fit its flash sector: the longest patterns give way (4 bars -> 2 -> 1)
    def packed_size(secs):
        return 32 + sum(16 + (x["steps"] + 3) // 4 + x["steps"] * 10 for x in secs)
    by_name = {n: (bars, outro) for n, bars, _, _ in [(n, b, f, l) for n, b, f, l in secs]}
    limits = {x["name"]: 4 for x in out}
    while packed_size(out) > SNG_MAX:
        big = max(out, key=lambda x: (x["steps"], limits[x["name"]]))
        if limits[big["name"]] <= 1:
            print(f"  the song does not fit ({packed_size(out)} B of {SNG_MAX}): drop sections (--sections)")
            break
        limits[big["name"]] //= 2
        name = big["name"]
        bars, outro = by_name[name]
        sec, p, score, has_fill, dropped = make_section(name, bars, outro, limits[name])
        out[out.index(big)] = sec
        print(f"  {name}: pattern cut to {p} bar{'s' if p > 1 else ''} ({score:.0%}) so the song fits its flash sector")
    if not any(s.get("outro") for s in out):
        print("  no section named OUTRO / ENDING / END: the ending stops on the next bar (set \"outro\": true in the JSON)")
    spec = {"name": title, "bpm": int(song.tempo), "kit": a.kit, "swing": 50, "drum_level": 100, "drum_reverb": 20,
            "sections": out}
    if a.out:
        with open(a.out, "w") as f:
            json.dump(spec, f, indent=1)
        print(f"wrote {a.out}: {len(out)} sections")


if __name__ == "__main__":
    main()
