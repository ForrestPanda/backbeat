#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The BackBeat logo: four beat bars in the device's track colours (blue, green, yellow, orange), the 2 and the
4 tall: the backbeat, the snare on 2 and 4, standing on a floor line; and a geometric monoline wordmark,
BACKBEAT, round-capped as SLOOP's was (BackBeat is a fork of SLOOP, itself based on Felucca).

  tools/gen_logo.py OUT.h          firmware boot splash (RLE, 16-colour palette, RGB565)
  tools/gen_logo.py --assets DIR   backbeat-logo.svg / .png, backbeat-icon.svg / .png, backbeat-splash.png
  tools/gen_logo.py --icons DIR    the web app's icons: icon-192.png, icon-512.png, icon-maskable-512.png
"""
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw

BLUE, GREEN, YELLOW, ORANGE = (40, 124, 255), (30, 204, 112), (255, 198, 24), (255, 98, 26)
WHITE, BLACK = (242, 242, 242), (0, 0, 0)
HEX = lambda c: "#%02X%02X%02X" % c
BARS = [BLUE, GREEN, YELLOW, ORANGE]

# ---- geometry (units: the icon is 240 x 240; the wordmark's cap height is 72, its stroke 12) ---------------
BAR_W, BAR_GAP, BAR_R = 36, 14, 8
BAR_H = (80, 150, 80, 150)                       # beats 1..4: the 2 and the 4 accented
BASE_Y, FLOOR_Y, FLOOR_H = 186, 204, 12          # the bars' foot, the floor line under them
CH, SW, GAP = 72, 12, 16                         # cap height, stroke, space between letters


# a glyph: its width (stroke centres, the stroke's half added on each side when laid out) and its strokes:
# ("l", x0, y0, x1, y1) a line, ("a", cx, cy, rx, ry, start, end) an arc in degrees, clockwise on screen from
# 0 = 3 o'clock (as PIL draws it); y down, 0 the cap line, CH the baseline
def _b():
    r = CH / 4
    x1, x2 = 22, 26                              # the bowls' straight parts (the lower bowl a little wider)
    return x2 + r, [("l", 0, 0, 0, CH), ("l", 0, 0, x1, 0), ("a", x1, r, r, r, 270, 450), ("l", x1, CH / 2, 0, CH / 2),
                    ("l", 0, CH / 2, x2, CH / 2), ("a", x2, 3 * r, r, r, 270, 450), ("l", x2, CH, 0, CH)]


def _a():
    w, y = 54, CH * 0.64
    x = w * (1 - y / CH) / 2                     # the left leg at the crossbar's height
    return w, [("l", 0, CH, w / 2, 0), ("l", w / 2, 0, w, CH), ("l", x, y, w - x, y)]


def _c():
    w = 50
    return w, [("a", w / 2, CH / 2, w / 2, CH / 2, 50, 310)]


def _k():
    w = 48
    return w, [("l", 0, 0, 0, CH), ("l", w, 0, 0, CH * 0.6), ("l", w * 0.32, CH * 0.43, w, CH)]


def _e():
    w = 42
    return w, [("l", 0, 0, 0, CH), ("l", 0, 0, w, 0), ("l", 0, CH / 2, w * 0.8, CH / 2), ("l", 0, CH, w, CH)]


def _t():
    w = 50
    return w, [("l", 0, 0, w, 0), ("l", w / 2, 0, w / 2, CH)]


GLYPHS = {"B": _b(), "A": _a(), "C": _c(), "K": _k(), "E": _e(), "T": _t()}
WORD = "BACKBEAT"


def word_width():
    return sum(GLYPHS[c][0] + SW for c in WORD) + GAP * (len(WORD) - 1)


def word_strokes(ox, oy):
    """the wordmark's strokes, placed (ox, oy: the top left of its box)"""
    out, x = [], ox + SW / 2
    for c in WORD:
        w, strokes = GLYPHS[c]
        for s in strokes:
            if s[0] == "l":
                out.append(("l", x + s[1], oy + s[2], x + s[3], oy + s[4]))
            else:
                out.append(("a", x + s[1], oy + s[2], s[3], s[4], s[5], s[6]))
        x += w + SW + GAP
    return out


def bar_boxes(ox, oy, k):
    x0 = ox + (240 - 4 * BAR_W - 3 * BAR_GAP) / 2 * k
    out = []
    for i, (c, h) in enumerate(zip(BARS, BAR_H)):
        x = x0 + i * (BAR_W + BAR_GAP) * k
        out.append((c, [x, oy + (BASE_Y - h) * k, x + BAR_W * k, oy + BASE_Y * k]))
    return out


# ---- raster -------------------------------------------------------------------------------------------
def rrect(d, box, r, fill):
    """rounded rectangle (ImageDraw.rounded_rectangle needs Pillow 8.2)"""
    x0, y0, x1, y1 = box
    r = min(r, (x1 - x0) / 2, (y1 - y0) / 2)
    d.rectangle([x0 + r, y0, x1 - r, y1], fill=fill)
    d.rectangle([x0, y0 + r, x1, y1 - r], fill=fill)
    for cx, cy in ((x0 + r, y0 + r), (x1 - r, y0 + r), (x0 + r, y1 - r), (x1 - r, y1 - r)):
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill)


def draw_icon(d, ox, oy, k):
    for c, box in bar_boxes(ox, oy, k):
        rrect(d, box, BAR_R * k, c)
    rrect(d, [ox + 22 * k, oy + FLOOR_Y * k, ox + 218 * k, oy + (FLOOR_Y + FLOOR_H) * k], FLOOR_H / 2 * k, WHITE)


def draw_word(d, ox, oy, k, col=WHITE):
    w = SW * k

    def cap(x, y):
        d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=col)

    for s in word_strokes(0, 0):
        if s[0] == "l":
            x0, y0, x1, y1 = (ox + v * k if i % 2 == 0 else oy + v * k for i, v in enumerate(s[1:]))
            d.line([(x0, y0), (x1, y1)], fill=col, width=max(1, round(w)))
            cap(x0, y0)
            cap(x1, y1)
        else:
            _, cx, cy, rx, ry, a0, a1 = s
            cx, cy, rx, ry = ox + cx * k, oy + cy * k, rx * k, ry * k
            d.arc([cx - rx - w / 2, cy - ry - w / 2, cx + rx + w / 2, cy + ry + w / 2], start=a0, end=a1, fill=col,
                  width=max(1, round(w)))
            for a in (a0, a1):
                cap(cx + rx * math.cos(math.radians(a)), cy + ry * math.sin(math.radians(a)))


def render(kind, W, H, k, bg=BLACK, ss=4, wk=None):
    """h: the icon and the wordmark side by side; v: the icon over the wordmark (wk its scale); i: the icon"""
    im = Image.new("RGB", (W * ss, H * ss), bg)
    d = ImageDraw.Draw(im)
    K = k * ss
    if kind == "h":
        draw_icon(d, 10 * K, (H * ss - 240 * K) / 2, K)
        draw_word(d, 270 * K, (H * ss - CH * K) / 2 + 20 * K, K)
    elif kind == "v":
        WK = (wk or k) * ss
        draw_icon(d, (W * ss - 240 * K) / 2, 0, K)
        draw_word(d, (W * ss - word_width() * WK) / 2, 236 * K, WK)
    else:
        draw_icon(d, (W * ss - 240 * K) / 2, (H * ss - 240 * K) / 2, K)
    return im.resize((W, H), Image.LANCZOS)


# ---- SVG (stroke centred on the path) ---------------------------------------------------------------------
def svg_icon(ox, oy):
    s = []
    for c, (x0, y0, x1, y1) in bar_boxes(ox, oy, 1.0):
        s.append(f'<rect x="{x0:.2f}" y="{y0:.2f}" width="{x1 - x0:.2f}" height="{y1 - y0:.2f}" rx="{BAR_R}" fill="{HEX(c)}"/>')
    s.append(f'<rect x="{ox + 22:.2f}" y="{oy + FLOOR_Y:.2f}" width="196" height="{FLOOR_H}" rx="{FLOOR_H / 2}" '
             f'fill="{HEX(WHITE)}"/>')
    return s


def svg_word(ox, oy):
    col, s = HEX(WHITE), []
    for t in word_strokes(ox, oy):
        if t[0] == "l":
            s.append(f'<line x1="{t[1]:.2f}" y1="{t[2]:.2f}" x2="{t[3]:.2f}" y2="{t[4]:.2f}" stroke="{col}" '
                     f'stroke-width="{SW}" stroke-linecap="round"/>')
        else:
            _, cx, cy, rx, ry, a0, a1 = t
            p = (cx + rx * math.cos(math.radians(a0)), cy + ry * math.sin(math.radians(a0)))
            q = (cx + rx * math.cos(math.radians(a1)), cy + ry * math.sin(math.radians(a1)))
            large = 1 if (a1 - a0) > 180 else 0
            s.append(f'<path d="M{p[0]:.2f} {p[1]:.2f} A{rx:.2f} {ry:.2f} 0 {large} 1 {q[0]:.2f} {q[1]:.2f}" '
                     f'fill="none" stroke="{col}" stroke-width="{SW}" stroke-linecap="round"/>')
    return s


def svg(kind):
    if kind == "h":
        W, H = 290 + round(word_width()), 260
        body = svg_icon(10, 10) + svg_word(270, (H - CH) / 2 + 20)
    else:
        W, H = 240, 240
        body = svg_icon(0, 0)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" role="img" '
            f'aria-label="BackBeat">\n<rect width="{W}" height="{H}" fill="#000"/>\n' + "\n".join(body) + "\n</svg>\n")


# ---- firmware splash ----------------------------------------------------------------------------------------
SPLASH_W, SPLASH_H = 240, 188
SPLASH_K, SPLASH_WK = 0.6, 0.38                  # the icon's scale, the wordmark's (as wide as the screen allows)
assert word_width() * SPLASH_WK <= SPLASH_W - 8, "the wordmark must fit the screen's width"
assert 236 * SPLASH_K + (CH + SW) * SPLASH_WK <= SPLASH_H, "the wordmark must fit under the icon"


def rgb565(c):
    r, g, b = c
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def splash_header(path):
    im = render("v", SPLASH_W, SPLASH_H, SPLASH_K, wk=SPLASH_WK)
    # a fixed palette: black, and each logo colour at 1/3, 2/3 and full (the anti-aliased edges)
    cols = [BLACK]
    for c in (WHITE, BLUE, GREEN, YELLOW, ORANGE):
        cols += [tuple(round(v * f) for v in c) for f in (1 / 3, 2 / 3, 1.0)]
    src = im.tobytes()
    px = []
    cache = {}
    for i in range(0, len(src), 3):
        p = (src[i], src[i + 1], src[i + 2])
        if p not in cache:
            cache[p] = min(range(16), key=lambda k: sum((p[j] - cols[k][j]) ** 2 for j in range(3)))
        px.append(cache[p])
    rle = bytearray()                                 # (run - 1) << 4 | index, runs of 1..16
    i = 0
    while i < len(px):
        j = i
        while j < len(px) and px[j] == px[i] and j - i < 16:
            j += 1
        rle.append(((j - i - 1) << 4) | px[i])
        i = j
    L = ["/* generated by tools/gen_logo.py: the BackBeat boot splash */", "#pragma once", "#include <stdint.h>",
         f"#define LOGO_SPLASH_W {SPLASH_W}", f"#define LOGO_SPLASH_H {SPLASH_H}",
         "static const uint16_t LOGO_SPLASH_PAL[16] = {" + ", ".join(f"0x{rgb565(c):04X}" for c in cols) + "};",
         f"static const uint8_t LOGO_SPLASH_RLE[{len(rle)}] = {{"]
    for k in range(0, len(rle), 24):
        L.append("    " + ", ".join(str(b) for b in rle[k:k + 24]) + ",")
    L.append("};")
    Path(path).write_text("\n".join(L) + "\n")
    print(f"logo: splash {SPLASH_W}x{SPLASH_H}, {len(rle)} B RLE -> {path}")


def assets(out):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "backbeat-logo.svg").write_text(svg("h"))
    (out / "backbeat-icon.svg").write_text(svg("i"))
    render("h", round((290 + word_width()) * 2), 520, 2.0).save(out / "backbeat-logo.png")
    render("i", 512, 512, 512 / 240).save(out / "backbeat-icon.png")
    render("v", SPLASH_W, SPLASH_H, SPLASH_K, wk=SPLASH_WK).resize((SPLASH_W * 2, SPLASH_H * 2), Image.NEAREST) \
        .save(out / "backbeat-splash.png")
    print(f"logo: assets in {out}")


def icons(out):
    """the web app's icons (web/make_site.py copies them): the icon on black; the maskable one inside the 80 % safe
    zone (Android crops it to a circle or a squircle)"""
    out = Path(out)
    render("i", 192, 192, 192 / 240).save(out / "icon-192.png")
    render("i", 512, 512, 512 / 240).save(out / "icon-512.png")
    render("i", 512, 512, 512 / 240 * 0.72).save(out / "icon-maskable-512.png")
    print(f"logo: web icons in {out}")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--assets":
        assets(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "--icons":
        icons(sys.argv[2])
    elif len(sys.argv) == 2:
        splash_header(sys.argv[1])
    else:
        sys.exit(__doc__)
