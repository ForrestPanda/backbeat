#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""fm1-drummer: the built-in layered kit (KIT MULDJORD), rendered once from the source kits into assets/realkit/.

    tools/gen_realkit.py                 # render it (the source kits in fm1-mvave/kits, sox): WAVs + kit.json
    tools/gen_realkit.py --report        # what assets/realkit holds: lanes, layers, takes, lengths, bytes

The recipe is tools/realkit.json: per lane its source (a key of an SFZ kit, or a WAV), how many velocity layers
(1..4; spread over the source's layers, the hardest first: as the steps' levels hard, normal, soft, ghost), how
many round-robin takes (the source's first ones), its length, its floor and its sample rate; "mics", the
microphones to keep (the RIM: its close mic only, 2026-10-21: the others, the wires and the room, made a click a roll). Every hit is the
sum of its microphones (tools/mkkit.py's mic_gain: the drum's own mics full, overheads -3 dB, room -6 dB, the
other drums' -14 dB, triggers left out), resampled by sox, cut at its onset, its tail below -54 dB dropped, then
cut at the lane's length with a fade. A lane keeps its layers' natural levels: one gain for the whole lane, set
so its hardest hit peaks at the lane's gain (dB under full scale). The kit is fitted to the budget (bytes of
IMA ADPCM, 4 bits a sample): while it is over, the lane with the most bytes above its floor loses a tenth of
its length. The WAVs are what the firmware's build encodes (tools/gen_samples.py: set REAL) and what the web
editor's player plays, so the build never needs the source kits (some GB). Names: LANE-<layer><take>.wav, layer
1 the hardest, take a, b, c."""
import argparse
import json
import math
import os
import struct
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
KITS = ROOT.parent.parent / "kits"
OUT = ROOT / "assets" / "realkit"
sys.path.insert(0, str(HERE))
import mkkit  # noqa: E402
import sampleio as sio  # noqa: E402

LANES = mkkit.LANES
LANE_NOTE = mkkit.LANE_NOTE


def src_path(spec):
    where, _, rel = spec.partition(":")
    return (KITS if where == "kits" else ROOT) / rel


def spread(n, m):
    """m of n layers (1 = the hardest), spread from the hardest to the softest"""
    if m >= n:
        return list(range(1, n + 1))
    if m == 1:
        return [1]
    return [1 + round(i * (n - 1) / (m - 1)) for i in range(m)]


def sfz_hits(regions, key, layers, takes, from_layer=None, take_from=0):
    """[[regions of a hit] per take] per layer: the layers spread over the key's, the round robins in order"""
    lst = regions.get(str(key)) or []
    lovels = sorted({int(r.get("lovel", 1)) for r in lst}, reverse=True)
    if not lovels:
        raise SystemExit(f"key {key}: not in the kit")
    picks = [from_layer] if from_layer else spread(len(lovels), layers)
    out = []
    for p in picks:
        lv = lovels[min(p, len(lovels)) - 1]
        cands = [r for r in lst if int(r.get("lovel", 1)) == lv and "silence" not in r.get("sample", "")]
        rr = lambda r: (float(r.get("lorand", 0)), int(r.get("seq_position", 1)))
        keys = sorted({rr(r) for r in cands})
        keys = keys[take_from:] + keys[:take_from]
        out.append([[r for r in cands if rr(r) == k] for k in keys[:takes]])
    return out


def decode(path, rate, tmp):
    """a file as float mono at rate (sox, its best resampler)"""
    dst = os.path.join(tmp, f"{abs(hash((path, rate)))}.wav")
    if not os.path.exists(dst):
        r = subprocess.run(["sox", path, "-e", "floating-point", "-b", "32", "-c", "1", dst, "rate", "-v", str(rate)],
                           capture_output=True)
        if r.returncode:
            raise SystemExit(f"sox: {path}: {r.stderr.decode()[:200]}")
    return sio.read_any_wav(Path(dst))[1]


def mix(regs, rate, tmp, mics=None):
    """mics: only the microphones whose names hold one of these (a lane's "mics"; none: all of them)"""
    tracks = []
    for r in regs:
        if not os.path.exists(r["_path"]):
            continue
        if mics and not any(m.lower() in os.path.basename(r["_path"]).lower().rsplit("-", 1)[-1] for m in mics):
            continue
        mg = mkkit.mic_gain(r["_path"], len(regs))
        if mg is None:
            continue
        g = 10 ** ((float(r.get("volume", 0)) + mg) / 20)
        tracks.append([v * g for v in decode(r["_path"], rate, tmp)])
    if not tracks:
        raise SystemExit("a hit with no audio: " + ", ".join(r["_path"] for r in regs[:3]))
    n = max(len(t) for t in tracks)
    out = [0.0] * n
    for t in tracks:
        for i, v in enumerate(t):
            out[i] += v
    return out


def trim(x, rate):
    """from the onset (1 ms before it), the tail below -54 dB dropped"""
    pk = sio.peak(x)
    start = max(0, sio.onset(x, 0.02) - int(0.001 * rate))
    end = len(x)
    while end > start + 1 and abs(x[end - 1]) < pk * 0.002:
        end -= 1
    return x[start:end]


def cut(x, sec, rate):
    """at sec, with a fade (20 ms, or a sixth of a long sound's length: the cymbals ring out)"""
    n = int(sec * rate)
    if len(x) <= n:
        return x
    x = x[:n]
    f = max(int(0.02 * rate), int(n / 6) if sec >= 0.8 else 0)
    for i in range(f):
        x[n - f + i] *= (1.0 - i / f) ** 2
    return x


def render(recipe, tmp):
    """{lane: {"rate", "hits": [[float samples] per take] per layer}}"""
    srcs = {k: src_path(v) for k, v in recipe["sources"].items()}
    regions = {}
    out = {}
    jobs = []
    for lane, spec in recipe["lanes"].items():
        rate = spec["rate"]
        if "file" in spec:
            sr, x = sio.read_any_wav(srcs[spec["src"]] / spec["file"])
            out[lane] = {"rate": rate, "hits": [[trim(sio.resample(x, sr, rate), rate)]]}
            continue
        sfz = srcs[spec["src"]]
        if sfz not in regions:
            regions[sfz] = mkkit.sfz_regions(str(sfz))
        hits = sfz_hits(regions[sfz], spec["key"], spec.get("layers", 1), spec.get("takes", 1),
                        spec.get("from_layer"), spec.get("take_from", 0))
        out[lane] = {"rate": rate, "hits": [[None] * len(t) for t in hits]}
        for li, takes in enumerate(hits):
            for ti, regs in enumerate(takes):
                jobs.append((lane, li, ti, regs, rate, spec.get("mics")))
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        res = list(ex.map(lambda j: (j[:3], trim(mix(j[3], j[4], tmp, j[5]), j[4])), jobs))
    for (lane, li, ti), x in res:
        out[lane]["hits"][li][ti] = x
    return out


NOMINAL = {1: [127], 2: [127, 72], 3: [127, 100, 72], 4: [127, 100, 72, 42]}   # the velocity each layer stands for
                                                     # (drums.c: lvl_vel's hard, normal, soft, ghost)


def layer_db(nl, li, dyn):
    """a layer's level under the hardest: the velocity curve the other kits play (20 log v/127), times dyn"""
    return dyn * 20 * math.log10(NOMINAL[nl][li] / 127)


def level(recipe, kit):
    """each layer to its place on the lane's curve: the mean peak of its takes at the lane's gain plus layer_db (the
    sources' own range is wide: a ghost snare 25 dB under the hardest would vanish under a guitar); the takes keep
    their small differences; nothing clips"""
    dyn = recipe.get("dyn", 1.5)
    for lane, d in kit.items():
        g0 = 30000 * 10 ** (recipe["lanes"][lane]["gain"] / 20)
        nl = len(d["hits"])
        out = []
        for li, t in enumerate(d["hits"]):
            mean = sum(sio.peak(x) for x in t) / len(t)
            g = g0 * 10 ** (layer_db(nl, li, dyn) / 20) / mean
            out.append([[v * g for v in x] for x in t])
        top = max(sio.peak(x) for t in out for x in t)
        if top > 32000:
            out = [[[v * 32000 / top for v in x] for x in t] for t in out]
        d["hits"] = out


def nbytes(n):
    return (n + 1) // 2 + ((n + 1) // 2 & 1)     # (gen_samples.py: each sample on an even offset)


def fit(recipe, kit):
    """the lengths: each lane's own, then a tenth at a time off the lane with the most bytes above its floor"""
    sec = {lane: recipe["lanes"][lane]["len"] for lane in kit}

    def size(lane, s):
        r = kit[lane]["rate"]
        return sum(nbytes(min(len(x), int(s * r))) for t in kit[lane]["hits"] for x in t)

    budget = recipe["budget"]
    while sum(size(l, sec[l]) for l in kit) > budget:
        over = [(size(l, sec[l]) - size(l, recipe["lanes"][l]["min"]), l) for l in kit if sec[l] > recipe["lanes"][l]["min"] + 1e-6]
        if not over:
            raise SystemExit("the kit does not fit its budget even at the floors")
        lane = max(over)[1]
        sec[lane] = max(recipe["lanes"][lane]["min"], sec[lane] * 0.9)
    for lane, d in kit.items():
        d["hits"] = [[cut(x, sec[lane], d["rate"]) for x in t] for t in d["hits"]]
    return sec


def write_wav(path, x, rate):
    import wave
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
        w.writeframes(struct.pack(f"<{len(x)}h", *(max(-32768, min(32767, int(round(v)))) for v in x)))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--recipe", default=str(HERE / "realkit.json"))
    ap.add_argument("--out", default=str(OUT))
    ap.add_argument("--report", action="store_true", help="only list what the kit folder holds")
    a = ap.parse_args()
    out = Path(a.out)
    if a.report:
        meta = json.loads((out / "kit.json").read_text())
        total = 0
        for L in meta["lanes"]:
            b = sum(nbytes(n) for t in L["samples"] for n in t)
            total += b
            print(f"  {L['lane']:<8} {L['layers']} x {max(L['takes'])}  {L['rate']:>5} Hz  {max(n for t in L['samples'] for n in t) / L['rate']:.2f} s  {b:>6} B")
        print(f"{meta['name']}: {total} B of IMA ADPCM")
        return
    recipe = json.loads(Path(a.recipe).read_text())
    with tempfile.TemporaryDirectory() as tmp:
        kit = render(recipe, tmp)
    level(recipe, kit)
    sec = fit(recipe, kit)
    out.mkdir(parents=True, exist_ok=True)
    for p in out.glob("*.wav"):
        p.unlink()
    lanes, total = [], 0
    for lane in LANES:
        if lane not in kit:
            continue
        d = kit[lane]
        files, samples = [], []
        for li, t in enumerate(d["hits"]):
            files.append([])
            samples.append([])
            for ti, x in enumerate(t):
                name = f"{lane}-{li + 1}{'abcdefgh'[ti]}.wav"
                write_wav(out / name, x, d["rate"])
                files[-1].append(name)
                samples[-1].append(len(x))
                total += nbytes(len(x))
        lanes.append({"lane": lane, "note": LANE_NOTE[lane], "rate": d["rate"], "layers": len(files),
                      "takes": [len(t) for t in files], "vel": NOMINAL[len(files)], "files": files, "samples": samples})
        print(f"  {lane:<8} {len(files)} x {len(files[0])}  {d['rate']:>5} Hz  {sec[lane]:.2f} s")
    meta = {"name": recipe["name"], "lanes": lanes, "bytes": total}
    (out / "kit.json").write_text(json.dumps(meta, indent=1) + "\n")
    print(f"{recipe['name']}: {total} B of IMA ADPCM (budget {recipe['budget']}) in {out}")


if __name__ == "__main__":
    main()
