#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The README's screen strips (assets/screens/backbeat-songs/edit/setup.png) from the host tests' renders
(backbeat-live.png, the first, was made by hand from the same renders).

    sh tests/run_tests.sh          # renders every screen to build/host/*.ppm (ui_pages_test)
    python3 tools/gen_screens.py

Each strip is a few 240 x 240 screens side by side, doubled (nearest neighbour, so the pixel font stays
sharp), 24 px apart on white, as assets/screens/backbeat-live.png."""
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC, OUT = ROOT / "build" / "host", ROOT / "assets" / "screens"
SCALE, GAP = 2, 24

STRIPS = {
    "backbeat-songs.png": ("page-songs", "live-nosong", "live-cued"),
    "backbeat-edit.png": ("live-grid", "live-kit"),
    "backbeat-setup.png": ("setup-pedal", "setup-midi", "menu-bridge"),
}


def strip(names):
    shots = [Image.open(SRC / f"{n}.ppm").convert("RGB") for n in names]
    w, h = shots[0].width * SCALE, shots[0].height * SCALE
    out = Image.new("RGB", (len(shots) * w + (len(shots) - 1) * GAP, h), "white")
    for i, im in enumerate(shots):
        out.paste(im.resize((w, h), Image.NEAREST), (i * (w + GAP), 0))
    return out


def main():
    missing = [n for names in STRIPS.values() for n in names if not (SRC / f"{n}.ppm").exists()]
    if missing:
        sys.exit(f"gen_screens: no render of {', '.join(missing)} in {SRC} (run sh tests/run_tests.sh)")
    for name, names in STRIPS.items():
        strip(names).save(OUT / name, optimize=True)
        print(f"{OUT / name}: {', '.join(names)}")


if __name__ == "__main__":
    main()
