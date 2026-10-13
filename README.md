# fm1-drummer

A guitarist's accompaniment device on the M-VAVE FM-1: songs made of drum sections (intro, verse,
chorus, bridge, solo, outro), switched live from a pedalboard over the TRS MIDI jack, with fills on
the transitions. No backing tracks: the song's structure and the length of every section are decided
on stage.

A fork of [SLOOP](https://github.com/isod89/sloop-fm1) 2.6 (GPL-3.0, itself from
[Felucca](https://github.com/hugelton/Felucca)). SLOOP's manual is [SLOOP.md](SLOOP.md); everything
it describes still works. What this fork adds is in [docs/design.md](docs/design.md).

Status: **everything but the hardware (October 2026)**. The pedal control, songs in flash (compact, with a
time signature per section), the fills, the renderer, the Guitar Pro importer, the kit tool, the web editor's
Songs tab with its section editor and a player the real pedal can drive, the screen and a song screen on the
device are done and pass the host tests; the firmware builds and packages, and runs on the fm1-emu emulator.
Nothing here has run on an FM-1 yet: the TRS jack, the cable type and the speaker are still to be verified
(`docs/design.md`, milestone 7). The flash is full (about 100 bytes left): a bigger feature needs some of
SLOOP's taken out first.

## The pedal

Footswitches send MIDI on channel 16 (default) to the TRS jack or over USB:

| Message | Meaning |
| --- | --- |
| CC 20, value 0..3 | section A..D when the section playing has played its pattern out, the last bar of it the fill (a 1-bar section: the next bar, the rest of this one the fill); stopped: start with it. The section already playing: a fill request (as CC 21), so a section switch is a fill switch too |
| CC 27, value 0..3 | now, without waiting for the section's end: the rest of this bar and a whole fill bar, the section on the bar after |
| CC 21 | the fill from now to the end of the section's pattern (in its last bar: through the next cycle too), the section goes on |
| CC 22, value 0..3 | ending: the section playing ends (its last bar the fill), that section once as the outro, stop on the bar (other values: the section ends, then stop) |
| CC 23 | stop now |
| CC 24 / CC 25 | next / previous section (empty ones skipped, wrapping), with the fill; stopped: the first / the last |
| CC 26 | tap tempo |
| PC 0..7 | load that song: stopped, its tempo and kit now and its first section cued; playing, with the next section |
| PC 8..127 | no song: SLOOP's sections A..D again |

**Count-in.** A setting kept with the pedal channel (off by default): a section pressed while stopped first
clicks one or two bars at the song's tempo (the metronome's voice, the first beat of each bar accented), then
the loop starts with that section. CC 23 cancels it; another section pressed meanwhile only changes what starts.
`python3 tools/fm1_song.py --count-in 1`, or the Songs tab's pedal row; hear it with `fm1_render.py --count-in 1`.

**On the screen.** The TRACKS screen shows the section playing and its bar in the title's place; the
pedal's requests are said in the top bar ("NEXT: CHORUS", "SONG: BLACK", "ENDING: OUTRO").

**The song screen.** SAVE tapped on TRACKS opens it (SLOOP's song page): the song, its sections with the one
playing lit and the one cued underlined, a meter other than 4/4, the pedal's messages. Its keys are the pedal:
the white keys 1..8 the sections (as CC 20), 9..16 the same now (CC 27); the black keys F#3 previous, G#3 next,
A#3 fill, C#4 the ending, D#4 stop, F#4 tap; PLAY starts the section cued (with the count-in) or stops. With no
song loaded the white keys 1..8 load songs 1..8; KNOB 1 picks a slot and REC loads it, the last choice being
SLOOP's sections A..D and its chain editor (until the page is opened again).

**Songs.** A song is up to 8 named drum sections (intro, verse, chorus, bridge, solo, outro...) with its
tempo, kit and swing; 8 songs live in flash. Write one as JSON (`tests/example-song.json` is the model; `songs/` is yours) and send it over USB:

```
python3 tools/fm1_song.py tests/example-song.json            # check it
python3 tools/fm1_song.py tests/example-song.json --put 0    # into song slot 0 (the FM-1 stopped)
python3 tools/fm1_song.py --pedal-channel 16            # the pedal's MIDI channel, kept through power-off
python3 tools/fm1_song.py --count-in 1                  # one bar of clicks before a start from stopped (0, 1, 2)
```

Or, with the FM-1 connected by USB, open SLOOP's web editor (`web/editor.html`): its **Songs** tab lists
the eight slots, takes a song JSON by drop or file picker, saves one back as JSON, erases a slot, and sets
the pedal's MIDI channel. Its **From Guitar Pro or JSON** card runs `tools/gp2song.py` itself in the browser (Python
through Pyodide, loaded from the internet the first time), or opens a song JSON as it is (one saved from the
editor, or from `songs/`); a file dropped on that card or on the editor opens too. Its **Play** card plays a song
with the kit's pads as the pedal would: the section buttons are the footswitches, with fills, the ending, stop,
and the count-in as the device has it set; **MIDI in from** and **Listen to the pedal** let the real pedal (the
Ampero, or a USB MIDI interface between it and the computer) drive that player with the same CCs and PCs as the
FM-1, on the Pedal card's channel (PC n opens song n of the FM-1 connected, or of the simulated one), so the
live workflow can be rehearsed before the hardware. Its **Section editor** card edits a song by hand: names, order,
length, the time signature, the outro, split and merge, the hits on a grid with levels, ratchets and fill marks, the fill
conditions, columns copied and pasted, undo and redo (Ctrl+Z / Ctrl+Y), keys for the pen (n h o g x, r for the
ratchet, m for the mark, [ ] between sections), a lane's sound on a click of its name, "Play this section" to
loop it, and, for a song read from Guitar Pro, the file's bars to pick as a section's pattern or its fill; then
Play, Send or Save JSON. The
Drum kit page opens with the Muldjord Kit on its pads (served from `web/kit/`, CC BY 4.0, see `docs/kits.md`), and
the player plays those pads, or WAVs you pick. The page must be served, not opened as a file: from the project
root `python3 -m http.server 8000`, then `http://localhost:8000/web/editor.html?mock=1` for the simulated
FM-1 (serving `web/` itself works too: the importer is linked there).

Or take the drum track of a Guitar Pro file (.gp3 / .gp4 / .gp5; `pip install pyguitarpro`): the score's
section markers become the sections, each with the pattern its bars repeat and the bar that deviates as
the fill; edit the JSON afterwards:

```
python3 tools/gp2song.py "song.gp4" --out songs/song.json
```

Then PC 0 from the pedal loads it and CC 20 picks its sections (0 = the first). Hear it on the computer
first, played by the pedal through the firmware's own drum engine (after a `./build.sh`):

```
python3 tools/fm1_render.py tests/example-song.json --play "INTRO:2 VERSE:8 CHORUS:8 SOLO:8 OUTRO" --listen
```

Or see the device itself on the [fm1-emu](https://github.com/joakimeriksson/fm1-emu) emulator (checked out next to
`my_fw`): your songs imported into its flash (as `fm1_song.py --put` would put them on a device), the firmware
run with a script of keys, knobs and pedal messages into the TRS jack, the screen saved at the times you ask
(`build/emu/lcd_<ms>.png`) and the sound with `--wav`. The emulator runs about 30 times slower than the device
on a PC:

```
python3 tools/fm1_emu.py songs/black.json songs/time.json --wav build/emu/run.wav   # the demo script, slots 1 and 2
```

**A sampled kit of your own.** A folder of WAV files (any CC0 kit; `assets/samples-cc0/KIT` is one)
becomes the device's kit USR3+4: the files are sorted onto the 16 lanes by name, trimmed, cut to the
slots' 2 x 7.4 s and levelled:

```
python3 tools/mkkit.py ROCK path/to/wavs --image build/host/rock.img     # hear it: fm1_render.py --usrkit build/host/rock.img
python3 tools/mkkit.py ROCK path/to/wavs --load                           # into the FM-1 (USR3 and USR4)
```

An SFZ kit with velocity layers works too (`--sfz kit.sfz`: the hardest layer of each drum, FLAC decoded
with sox; `--layers` adds a soft layer for ghost and soft hits, `--hires HAT,PEDAL,RIM` keeps the bright short
sounds at 44.1 kHz); `docs/kits.md` has the kits tried, the chosen one (the Muldjord Kit, CC BY 4.0) and the recipes.
Songs then use that kit by its index (`"kit": 41` in the JSON, the kit after the built-in and SYN kits).
The songs live in the flash of the USR1 slot, which the drummer gives up (USR2 stays free). With no song loaded the
pedal plays SLOOP's own sections A..D (the four project slots). **Fills** are marks on the hits of a section's
pattern: a hit marked "only in a fill" plays only during the transition (or CC 21, or the device's GLO fill keys),
one marked "not in a fill" rests then, mark by mark in the editor's grid or lane by lane in the JSON (`"fills"`);
SLOOP's own whole-step conditions still work on top. Built for the Hotone Ampero II Stage (each footswitch sends up to three CC / PC messages), but any
pedalboard with a MIDI out does.

## Build and test

```
./build.sh                   # build/felucca.bin, build/felucca.fwsc (toolchain and SDK: BUILDING.md)
sh tests/run_tests.sh        # SLOOP's host tests plus tests/pedal_test.c
```

The TRS MIDI input is SLOOP's (`FELUCCA_UART=1`, in use since SLOOP 2.3). Install as SLOOP
([BUILDING.md](BUILDING.md)): at your own risk.

## Layout

| Path | What |
| --- | --- |
| `docs/design.md` | the model, the pedal command map, milestones, flash budget, review notes, open questions |
| `docs/ampero-setup.md` | what to put into the Ampero II Stage's footswitches, the cable, what to check with the FM-1 |
| `docs/kits.md` | the drum kits: the chosen Muldjord Kit (CC BY 4.0, with Black Pearl's hats and Red Zeppelin's ride), the others tried, the recipes, the slot budget |
| `firmware/src/pedal.c` | the pedal: CC / PC from the MIDI in queue to section, song and transport requests |
| `firmware/src/sng.c` | songs: the packed form ("SNG2": a byte a hit, a meter per section), read in place from flash, a section into the drum track on the bar, the pedal settings |
| `firmware/src/ui_drummer.c` | the song screen (the SONG page while a song is loaded): sections, the keys as the pedal |
| `tools/fm1_emu.py` | the firmware on the fm1-emu emulator, songs imported into its flash: a script of keys, knobs and pedal MIDI, screens and sound saved |
| `tools/fm1_song.py`, `songs/` | songs from JSON: check, pack, send, read back; the example and four from Guitar Pro tabs (Black, Jeremy, Another Brick in the Wall, Comfortably Numb) |
| `web/editor.html` | SLOOP's web editor with a Songs tab: the slots, the pedal channel, Guitar Pro import (Pyodide), the section editor, a player; `web/test_web.mjs` tests it against the simulated FM-1 |
| `web/kit/` | the kit the editor serves (the Muldjord Kit with two velocity layers), the files `tools/mkkit.py` makes the device image from |
| `tools/fm1_render.py`, `tests/song_render.c` | a song played by the pedal, rendered to a WAV on the host |
| `tools/gp2song.py`, `tests/gp2song_test.py` | the drum track of a Guitar Pro file into a song (markers, patterns, fills, levels, ratchets) |
| `tools/mkkit.py` | a sampled kit from a folder of WAVs or an SFZ kit onto USR3+4: lanes by name, two velocity layers, trim, cut, level; an image for the renderer, or the upload |
| `firmware/src/seq.c` | SLOOP's sequencer; the pedal hooks in its MIDI loop and `events_block` |
| `tests/pedal_test.c` | the pedal through the real sequencer: sections on the bar, fills, endings, next / previous, stop, tap |
| `tests/song_test.c` | songs through the real sequencer: the packed form's checks, PC, sections, the outro, back to A..D |
| everything else | SLOOP 2.6 as forked |

## Licence

GPL-3.0-only, as SLOOP (see [LICENSE](LICENSE) and [LICENSING.md](LICENSING.md) for the assets).
M-VAVE and FM-1 are trademarks of their owners; this project is not affiliated with M-VAVE or Hotone.
