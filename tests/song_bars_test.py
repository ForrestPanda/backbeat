#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/fm1_song.py bar_fit: a section is whole bars of its time signature (the device counts bars: a change
waits for the bar line); one that is not gets the signature whose one bar is its pattern"""
import sys
from fractions import Fraction
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import fm1_song  # noqa: E402

fails = 0


def check(ok, what):
    global fails
    print(f"song bars: {what:70s} {'ok' if ok else 'FAIL'}")
    fails += not ok


f = fm1_song.bar_fit
check(f(16, "1/16", "4/4") is None and f(64, "1/16", None) is None, "whole 4/4 bars: nothing to say")
check(f(4, "1/16", "4/4") == (Fraction(1, 4), "1/4"), "4 steps of 1/16 in 4/4: a quarter of a bar, \"1/4\" fits")
check(f(20, "1/16", "4/4") == (Fraction(5, 4), "5/4"), "20 steps of 1/16: 1 1/4 bars, \"5/4\"")
check(f(14, "1/16", "7/8") is None and f(28, "1/16", "7/8") is None, "7/8: 14 and 28 sixteenths are whole bars")
check(f(16, "1/16", "7/8") == (Fraction(8, 7), "8/8"), "16 sixteenths in 7/8: \"8/8\" (its own denominator first)")
check(f(12, "8T", "4/4") is None and f(6, "8T", "4/4") == (Fraction(1, 2), "2/4"), "8T: 12 a bar, 6 half of one (\"2/4\")")
check(f(1, "2BAR", "4/4") is None and f(3, "1/32", "4/4") == (Fraction(3, 32), None),
      "2BAR whole; 3 x 1/32 has no n/d with d 2..16 (only the warning)")
w = fm1_song.bar_warnings({"sections": [{"name": "INTRO", "steps": 4, "div": "1/16"},
                                        {"name": "VERSE", "steps": 16, "div": "1/16"}]})
check(len(w) == 1 and w[0].startswith("INTRO:") and '"time": "1/4"' in w[0], "the warning names the section and the fix")
print("song bars: " + ("FAILED" if fails else "PASS"))
sys.exit(1 if fails else 0)
