# Felucca licensing

Felucca is free software. Its **code** is licensed under the GNU General Public License,
version 3 only (`GPL-3.0-only`, full text in `LICENSE`). Its **assets** are not part of
that licence: the icon atlas `assets/icons.png`, the panel image `docs/panel.jpg` and the drum sounds made by
`tools/gen_waves.py` (the Hügelton Sample Pack) are Copyright (C) 2026 Hügelton Instruments,
all rights reserved. Their licence terms will be published later. SLOOP's firmware does not contain the
Hügelton Sample Pack: its sampled drum kit is made of CC0 recordings (`assets/samples-cc0/KIT`), and
`gen_waves.py` only feeds the SLICE engine's demo loop, which SLOOP does not build.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments

## What is code (GPL-3.0-only)

Every file in this tree that carries an `SPDX-License-Identifier: GPL-3.0-only` header:

- the firmware: `firmware/` (app, HAL, update loader)
- the build script and tools: `build.sh`, `tools/`
- the web pages (installer, editor) and their tests: `web/` (not the Fukiai font, below)
- the host tests: `tests/`

You may use, study, change and share it under the GPL. If you distribute Felucca, or
firmware derived from it, you must also give your recipients its complete corresponding
source under the same licence. That includes devices that ship with modified Felucca
inside.

## Additional permission (GPL-3.0 section 7)

As an additional permission under GPL-3.0 section 7, you may combine Felucca, or a work
based on it, with the Felucca Assets (above), and convey the combination.
This is allowed even though the Felucca Assets are not licensed under the GPL, provided
that:

- you follow the GPL for every part that is not a Felucca Asset; and
- you follow the terms published for the assets.

The Felucca Assets are data (wavetables, icons, sample data). They are not program
code. A firmware image built from the GPL sources with replacement assets, or with no
assets, is entirely governed by the GPL.

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Instrument and drum samples (Versilian Studios VSCO-2 CE, VCSL; Sonic Pi: SCRCH); fm1-drummer builds only the drum kit (`KIT`) into the firmware | CC0 1.0 | `assets/samples-cc0/`, provenance in `ATTRIBUTION.txt` there |
| Terminus font 8x16: the FM-1's screen (Terminus Bold, ter-u16b, SLOOP 2.6), and the web editor's font (the regular ter-u16n; `tools/gen_webfont.py` makes it a TrueType font inlined in `web/editor.html`) | SIL OFL 1.1 | `assets/fonts/ter-u16b.bdf`, `assets/fonts/ter-u16n.bdf`, `assets/fonts/Terminus-LICENSE.txt` |
| Fukiai icon font (Hügelton Instruments), web editor only | MIT | `web/fukiai.ttf`, `web/FUKIAI-LICENSE.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

fm1-drummer is drums only (since 2026-10-14, `docs/drums-only.md`): SLOOP's synth engines are not in this tree any
more, and with them the material only they used: CrispyZebra (the PHASE engine, GPL-3.0), klattsch (a design
reference for the VOICE engine), msfa from Dexed (the FM6 engine, Apache-2.0) and DaisySP / Rings (the PHYS engine,
MIT). SLOOP upstream credits them where its engines are.

## Contributions

Contributions are welcome under GPL-3.0-only. By submitting one, you agree that it may be
combined with the Felucca Assets under the section 7 permission above.

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments.

"M-VAVE" and "FM-1" are trademarks of their respective owners. Felucca is independent
firmware that runs on FM-1 hardware. It is not affiliated with, endorsed by or supported
by those owners.

## Radio

Felucca never enables the Bluetooth / Wi-Fi radio of the hardware.

## BackBeat (fm1-drummer)

`web/kit/`: the MuldjordKit by Lars Muldjord and the DrumGizmo team (drumgizmo.org; the SFZ port by kinwie),
CC BY 4.0, one sound a lane as `tools/mkkit.py` makes them; the three hi-hat lanes from AVL Drumkits "Black Pearl"
by Glen MacArthur, CC BY-SA 3.0; CLAP and COWBELL from the CC0 studio recordings (see `web/kit/ATTRIBUTION.txt` and `docs/kits.md`). Preloaded on the editor's Drum kit page and
played by its Songs tab.

`assets/realkit/` (also served as the editor's `realkit/`, and built into the firmware as KIT MULDJORD): the same
sources rendered by `tools/gen_realkit.py` with their velocity layers and round robins: the MuldjordKit and the DRSKit's
snare, snare edge and rim (DrumGizmo, CC BY 4.0),
AVL Drumkits "Black Pearl" (hi-hats) and "Red Zeppelin" (ride) by Glen MacArthur (CC BY-SA 3.0), the CC0 studio
recordings (see `assets/realkit/ATTRIBUTION.txt`). A firmware package carries these sounds: share it with that credit.
