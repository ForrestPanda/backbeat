#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The FM-1 panel with what every control does in BackBeat: assets/panel/backbeat-panel.svg (the README, the
site's cheat sheet page) and a PNG next to it for places that take no SVG.

    python3 tools/gen_panel.py            # writes the SVG, and the PNG when rsvg-convert is there

The panel is drawn after the fm1-emu emulator's geometry (crates/fm1-live/src/panel.rs, itself after Felucca's
controls photo): MASTER, SELECT, PRESETS and ALGORITHM top left with OCT- / OCT+ under them, the screen in the
middle, KNOB 1..4 and two rows of six buttons on the right, the 27 keys (F3..G5) along the bottom. Every control
carries its LIVE role in colour, the role that matters on stage; the four columns under the panel say what the
same controls do on SONGS, SECTIONS and EDIT, and what the holds do. Plain SVG text (the system's sans-serif),
no fonts embedded. Edit the tables (ROLES, COLUMNS) and run again."""
import shutil
import subprocess
from pathlib import Path
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "assets" / "panel"

W = 1280
BG, BODY, TRAY, LINE = "#121316", "#2a2c31", "#1a1b1e", "#3c3f46"
INK, DIM, SILK = "#f2f2f2", "#9a9ca4", "#c9c6bd"
BLUE, GREEN, YELLOW, ORANGE, RED = "#4d93ff", "#1ecc70", "#ffc618", "#ff7a3a", "#ff4d55"
FONT = "'Helvetica Neue', Helvetica, Arial, 'Liberation Sans', sans-serif"

# the LIVE role of every control (short: it sits on the panel), and its colour
ROLES = {
    "MASTER": ("volume", YELLOW), "SELECT": ("tempo", YELLOW),
    "PRESETS": ("kit", YELLOW), "ALGORITHM": ("song: previous / next", YELLOW),
    "KNOB 1": ("tempo", YELLOW), "KNOB 2": ("kit", YELLOW), "KNOB 3": ("drum level", YELLOW), "KNOB 4": ("swing", YELLOW),
    "FX": ("effects", BLUE), "SEL": ("SONGS", BLUE), "ENV": ("PREV section", GREEN), "LFO": ("NEXT section", GREEN),
    "EDIT": ("SECTIONS", BLUE), "GLO": ("SETUP", BLUE),
    "HOME": ("LIVE, its layout", BLUE), "SAVE": ("hold: save", ORANGE), "ARP": ("hold: roll", DIM),
    "SEQ": ("EDIT, the grid", BLUE), "PLAY": ("play / stop", GREEN), "REC": ("hold: clear", DIM),
    "OCT-": ("back, undo", DIM), "OCT+": ("held: next bar", ORANGE),
    "OCT- 2": ("at power-on: rescue", DIM), "OCT+ 2": ("on SONGS: load", ORANGE),
}
WHITE_ROLE = ("sections 1 .. 16", GREEN)                      # white key n: section n, when the one playing ends
BLACK = {1: ("FILL", ORANGE), 3: ("FILL next bar", ORANGE), 8: ("BREAK", RED), 22: ("ENDING", RED), 25: ("TAP tempo", GREEN)}   # key id - 14
BLACK_OTHER = ("FILL", ORANGE)

# the columns under the panel: (title, colour, rows); a row: (control, what)
COLUMNS = [
    ("LIVE", GREEN, [
        ("white key n", "section n, when the one playing ends; with OCT+ held, on the next bar"),
        ("F#3", "fill to the end of the bar (CC 21); with OCT+ held, the whole next bar"),
        ("G#3", "fill on the whole next bar"),
        ("C#4", "break: the section comes back in through its break"),
        ("D#5", "the ending: the song's outro once, then the stop (no outro: the section ends, then the stop)"),
        ("F#5", "tap tempo (its light flashes the beat)"),
        ("other black keys", "a fill, as F#3"),
        ("ENV / LFO", "the previous / next section, no fill"),
        ("PLAY", "start the section cued, after the count-in; playing: stop"),
        ("HOME", "the other layout: the bar of the section big"),
        ("KNOB 1..4", "tempo, kit, drum level, swing (SELECT: tempo; PRESETS: kit)"),
        ("ALGORITHM", "the previous / next song of the page chosen on SONGS"),
    ]),
    ("SONGS  (SEL)", BLUE, [
        ("SEL", "the other page: USER (your 16 slots) / FACTORY (the 16 grooves)"),
        ("ENV / LFO", "USER / FACTORY outright"),
        ("KNOB 1, SELECT", "pick a row"),
        ("REC, OCT+", "load it, back to LIVE"),
        ("white key n", "load song n of the page"),
        ("EDIT", "that song's SECTIONS; on an empty slot: a new song"),
        ("HOME", "LIVE"),
    ]),
    ("SECTIONS  (EDIT)", BLUE, [
        ("KNOB 1, white keys", "pick the section the grid edits (playing: cued)"),
        ("KNOB 2 / 3 / 4", "its steps, its division (the hits keep their beat), the tempo"),
        ("SEL", "copy it after itself"),
        ("ENV / LFO", "move it left / right"),
        ("OCT+", "the outro mark on / off"),
        ("REC held 2 s", "take it out of the song"),
        ("EDIT", "the grid; HOME: LIVE"),
    ]),
    ("EDIT, the grid  (SEQ)", ORANGE, [
        ("KNOB 1 / 2", "the sound, the step"),
        ("KNOB 3", "HIT: off, ghost, soft, norm, hard"),
        ("KNOB 4", "ratchet x1 .. x4"),
        ("keys", "the steps of the grid; SEQ again: GRID / KIT"),
        ("OCT- / OCT+ held", "ghost / hard hits"),
        ("SAVE held", "the section into its song, the knobs' tempo, kit, swing, level, reverb"),
        ("SEQ held", "the step layer (DIV re-grids); EDIT held: erase; FX held: throws; ARP held: roll"),
        ("EDIT + OCT-", "undo"),
        ("GLO", "SETUP: pedal channel, count-in, MIDI, screen, system"),
    ]),
]

svg = []


def text(x, y, s, size=13, fill=INK, anchor="start", weight=400, family=FONT, opacity=1.0):
    svg.append(f'<text x="{x:.1f}" y="{y:.1f}" font-family="{family}" font-size="{size}" font-weight="{weight}" '
               f'fill="{fill}" text-anchor="{anchor}" opacity="{opacity}">{escape(s)}</text>')


def knob(cx, cy, r, name, role):
    svg.append(f'<circle cx="{cx}" cy="{cy}" r="{r + 4}" fill="{TRAY}"/>')
    svg.append(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="#45484f" stroke="#5c6069" stroke-width="2"/>')
    svg.append(f'<line x1="{cx}" y1="{cy - r + 6}" x2="{cx}" y2="{cy - r * 0.45:.1f}" stroke="{INK}" stroke-width="3" stroke-linecap="round"/>')
    text(cx, cy + r + 18, name, 11, SILK, "middle", 600)
    text(cx, cy + r + 34, role[0], 13, role[1], "middle", 600)


def button(x, y, w, h, name, role, role_above):
    svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" fill="#3d4047" stroke="#575b64" stroke-width="1.5"/>')
    text(x + w / 2, y + h / 2 + 4, name, 11, SILK, "middle", 600)
    ty = y - 10 if role_above else y + h + 17
    text(x + w / 2, ty, role[0], 11.5, role[1], "middle", 600)


def wrap(what, n=50):
    """the column text in lines of about n characters"""
    words, line, lines = what.split(" "), "", []
    for w in words:
        if len(line) + len(w) + 1 > n and line:
            lines.append(line); line = w
        else:
            line = (line + " " + w).strip()
    lines.append(line)
    return lines


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    panel_h = 820
    legend_y = panel_h + 36
    H = legend_y + 40 + max(sum(1 + len(wrap(what)) for _, what in c[2]) * 14 + len(c[2]) * 4 for c in COLUMNS) + 40
    svg.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
               f'font-family="{FONT}">')
    svg.append(f'<title>The FM-1 panel in BackBeat: what every knob, button and key does</title>')
    svg.append(f'<rect width="{W}" height="{H}" fill="{BG}"/>')
    # the body
    svg.append(f'<rect x="20" y="14" width="{W - 40}" height="{panel_h - 28}" rx="26" fill="{BODY}" stroke="{LINE}" stroke-width="2"/>')
    text(40, 42, "M-VAVE FM-1", 12, SILK, "start", 600)
    text(W - 40, 42, "BackBeat 0.9.1: the controls on LIVE, the stage screen", 12, DIM, "end", 400)
    # the screen (240 x 240 at 2x), a LIVE sketch in it
    sx, sy = 370, 42
    svg.append(f'<rect x="{sx - 10}" y="{sy - 10}" width="500" height="500" rx="10" fill="{TRAY}"/>')
    svg.append(f'<rect x="{sx}" y="{sy}" width="480" height="480" fill="#000"/>')
    text(sx + 16, sy + 36, "BLACK  song 3", 20, "#9aa0ff", "start", 600)
    text(sx + 464, sy + 36, "127 BPM", 20, INK, "end", 600)
    for i in range(4):
        svg.append(f'<rect x="{sx + 16 + i * 30}" y="{sy + 52}" width="22" height="8" fill="{GREEN if i == 1 else "#2a2f3a"}"/>')
    text(sx + 16, sy + 150, "CHORUS", 64, INK, "start", 700)
    text(sx + 16, sy + 190, "BAR 3 / 8", 24, DIM, "start", 500)
    svg.append(f'<rect x="{sx + 16}" y="{sy + 206}" width="448" height="6" fill="#2a2f3a"/>')
    svg.append(f'<rect x="{sx + 16}" y="{sy + 206}" width="168" height="6" fill="{GREEN}"/>')
    text(sx + 16, sy + 256, "FILL > BRIDGE", 28, ORANGE, "start", 700)
    tiles = ["1 INTRO", "2 VERSE", "3 CHORUS", "4 BRIDGE", "5 SOLO", "6 VERSE 2", "7 CHORUS 2", "8 OUTRO"]
    for i, t in enumerate(tiles):
        tx, ty = sx + 16 + (i % 4) * 112, sy + 290 + (i // 4) * 58
        on = i == 2
        svg.append(f'<rect x="{tx}" y="{ty}" width="104" height="48" rx="4" fill="{"#1e3a2a" if on else "#15171c"}" '
                   f'stroke="{GREEN if on else "#2a2f3a"}" stroke-width="2"/>')
        text(tx + 8, ty + 30, t, 15, INK if on else DIM, "start", 600)
    text(sx + 16, sy + 450, "kit MULDJORD   pedal ch 1 •   NEXT: BRIDGE", 16, DIM, "start", 500)
    text(sx + 240, sy + 500 + 2, "the white keys are the tiles: key 3 is CHORUS", 12, DIM, "middle", 400)
    # the knobs
    knob(110, 135, 44, "MASTER", ROLES["MASTER"])
    knob(250, 135, 44, "SELECT", ROLES["SELECT"])
    knob(110, 295, 44, "PRESETS", ROLES["PRESETS"])
    knob(250, 295, 44, "ALGORITHM", ROLES["ALGORITHM"])
    for i in range(4):
        knob(935 + i * 92, 120, 36, f"KNOB {i + 1}", ROLES[f"KNOB {i + 1}"])
    # OCT- / OCT+ (two lines each)
    button(62, 420, 112, 44, "OCT-", ROLES["OCT-"], False)
    button(186, 420, 112, 44, "OCT+", ROLES["OCT+"], False)
    text(118, 497, ROLES["OCT- 2"][0], 10.5, ROLES["OCT- 2"][1], "middle", 500)
    text(242, 497, ROLES["OCT+ 2"][0], 10.5, ROLES["OCT+ 2"][1], "middle", 500)
    # the two rows of six
    rows6 = [["FX", "SEL", "ENV", "LFO", "EDIT", "GLO"], ["HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"]]
    for r, row in enumerate(rows6):
        for i, name in enumerate(row):
            x, y = 906 + i * 58, 262 + r * 96
            above = r == 0
            # the roles of a row, staggered: every other one a line further out, so neighbours do not touch
            role = ROLES[name]
            svg.append(f'<rect x="{x}" y="{y}" width="50" height="64" rx="6" fill="#3d4047" stroke="#575b64" stroke-width="1.5"/>')
            text(x + 25, y + 36, name, 11, SILK, "middle", 600)
            far = i % 2 == 1
            ty = (y - 12 - (16 if far else 0)) if above else (y + 64 + 17 + (16 if far else 0))
            text(x + 25, ty, role[0], 11.5, role[1], "middle", 600)
            lx = x + 25
            ly0, ly1 = (y - 4, ty + 3) if above else (y + 68, ty - 11)
            if far:
                svg.append(f'<line x1="{lx}" y1="{ly0}" x2="{lx}" y2="{ly1}" stroke="{role[1]}" stroke-width="1" opacity="0.6"/>')
    # the keys
    kw, gap, x0 = 62, 11, 61
    whites = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]
    names = ["F3", "G3", "A3", "B3", "C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5", "D5", "E5", "F5", "G5"]
    for i, n in enumerate(whites):
        x = x0 + i * (kw + gap)
        svg.append(f'<rect x="{x}" y="680" width="{kw}" height="106" rx="7" fill="#e8e4dc" stroke="#b9b4aa" stroke-width="1.5"/>')
        text(x + kw / 2, 770, str(i + 1), 18, GREEN, "middle", 700)
        text(x + kw / 2, 745, names[i], 10, "#8a857c", "middle", 500)
    blacks = [(1, 0), (3, 1), (5, 2), (8, 4), (10, 5), (13, 7), (15, 8), (17, 9), (20, 11), (22, 12), (25, 14)]
    bnames = {1: "F#3", 3: "G#3", 5: "A#3", 8: "C#4", 10: "D#4", 13: "F#4", 15: "G#4", 17: "A#4", 20: "C#5", 22: "D#5", 25: "F#5"}
    for n, after in blacks:
        cx = x0 + (after + 1) * (kw + gap) - gap // 2
        x = cx - kw // 2
        svg.append(f'<rect x="{x}" y="574" width="{kw}" height="94" rx="7" fill="#2b2b30" stroke="#4a4a52" stroke-width="1.5"/>')
        text(cx, 655, bnames[n], 10, "#8a8a92", "middle", 500)
        role = BLACK.get(n, BLACK_OTHER)
        lines = role[0].split(" ")
        if len(lines) == 1:
            text(cx, 612, lines[0], 11, role[1], "middle", 700)
        else:
            text(cx, 604, lines[0], 11, role[1], "middle", 700)
            text(cx, 620, " ".join(lines[1:]), 9.5, role[1], "middle", 600)
    text(x0, 806, "white keys: " + WHITE_ROLE[0] + ", when the one playing ends (OCT+ held: on the next bar)", 12, WHITE_ROLE[1], "start", 600)
    text(x0 + 16 * (kw + gap) - gap, 806, "black keys: fills, the break, the ending, the tap tempo", 12, ORANGE, "end", 600)
    # the columns
    cw = (W - 40) / len(COLUMNS)
    for c, (title, colour, rws) in enumerate(COLUMNS):
        x = 20 + c * cw + 8
        text(x, legend_y, title, 13, colour, "start", 700)
        svg.append(f'<line x1="{x}" y1="{legend_y + 8}" x2="{x + cw - 24}" y2="{legend_y + 8}" stroke="{colour}" stroke-width="1.5" opacity="0.6"/>')
        y = legend_y + 30
        for ctl, what in rws:
            text(x, y, ctl, 11.5, INK, "start", 700)
            y += 14
            for ln in wrap(what):
                text(x, y, ln, 11.5, DIM, "start", 400)
                y += 14
            y += 4
    text(W - 20, H - 16, "BackBeat, a fork of SLOOP (Felucca). tools/gen_panel.py", 10.5, DIM, "end", 400, opacity=0.8)
    svg.append("</svg>")
    out = OUT / "backbeat-panel.svg"
    out.write_text("\n".join(svg) + "\n", encoding="utf-8")
    print(f"{out} ({out.stat().st_size} B)")
    if shutil.which("rsvg-convert"):
        png = OUT / "backbeat-panel.png"
        subprocess.run(["rsvg-convert", "-w", str(W * 2), "-o", str(png), str(out)], check=True)
        print(f"{png} ({png.stat().st_size} B)")


if __name__ == "__main__":
    main()
