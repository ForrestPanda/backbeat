<p align="center"><img src="assets/logo/backbeat-logo.png" alt="BackBeat" width="440"></p>

# BackBeat

BackBeat (this repository: fm1-drummer) is a guitarist's accompaniment device on the M-VAVE FM-1: songs
made of drum sections (intro, verse, chorus, bridge, solo, outro), switched live from a pedalboard over
the TRS MIDI jack, with fills where you ask for them. No backing tracks: the song's structure and the length of every section are decided
on stage.

A fork of [SLOOP](https://github.com/isod89/sloop-fm1) 2.6 (GPL-3.0, itself from
[Felucca](https://github.com/hugelton/Felucca)), **drums only**: SLOOP's synth side (the synth engines and
presets, the sampled instruments, the punch FX, the SLICER, live recording, the arpeggiator, scales and chords,
three of its four tracks) is gone, see [docs/drums-only.md](docs/drums-only.md). What is left of SLOOP is its
drum track, sequencer and sections (SLOOP's own manual is upstream). What this fork adds is in
[docs/design.md](docs/design.md). The firmware reports itself as BACKBEAT 1.0 (the editor's INFO keeps its
`FELUCCA ` prefix, and the USB port names and the backup format stay SLOOP's, so the tools keep working).

Status: **everything but the hardware (October 2026)**. The pedal control, songs in flash (compact, with a
time signature per section), the fills, the renderer, the Guitar Pro importer, the kit tool, the web editor's
Song tab with its section editor and a player the real pedal can drive, the device's own screens (LIVE, SONGS,
EDIT, SETUP) are done and pass the host tests; the firmware builds and packages, and runs on the fm1-emu emulator.
Nothing here has run on an FM-1 yet: the TRS jack, the cable type and the speaker are still to be verified
(`docs/design.md`, milestone 7). The firmware is 541 KB of its 581 KB slot, 290 KB of it the built-in layered
kit **MULDJORD**, the power-on kit and new songs' default (`"kit": 46`): up to four velocity layers a drum (hard,
normal, soft, ghost: the steps' levels), round-robin takes of the kick and snare (never the same twice running),
twelve voices so cymbals and toms ring on (`docs/kits.md`).

## The pedal

Footswitches send MIDI on channel 16 (default) to the TRS jack or over USB:

| Message | Meaning |
| --- | --- |
| CC 20, value 0..3 | section A..D when the section playing has played its pattern out (a 1-bar section: the next bar), no fill of its own; stopped: start with it. The section already playing: nothing |
| CC 27, value 0..3 | on the next bar, without waiting for the section's end, no fill of its own |
| CC 21 | the fill from now to the end of the section's pattern (in its last bar: through the next cycle too), the section goes on; sent with a CC 20 / 22 / 24 / 25 (either order), the fill into that change, ending with it |
| CC 22, value 0..3 | ending: the section playing ends, that section once as the outro, stop on the bar (other values: the song's flagged outro, or the section ends, then stop; the outro playing already: it plays to its end, then stop) |
| CC 23 | stop now |
| CC 24 / CC 25 | next / previous section (empty ones skipped, wrapping), as CC 20; stopped: the first / the last |
| CC 26 | tap tempo |
| PC 0..15 | load your song in slot 1..16: stopped, its tempo and kit now and its first section cued; playing, with the next section |
| PC 16..31 | load factory groove 1..16 (the same way) |
| PC 32..127 | no song: SLOOP's sections A..D again |

**Count-in.** A setting kept with the pedal channel (off by default): a section pressed while stopped first
clicks one or two bars at the song's tempo (the metronome's voice, the first beat of each bar accented), then
the loop starts with that section. CC 23 cancels it; another section pressed meanwhile only changes what starts.
`python3 tools/fm1_song.py --count-in 1`, the Song tab's pedal row, or SETUP > PEDAL on the device; hear it with
`fm1_render.py --count-in 1`.

**Songs.** A song is up to 16 named drum sections (intro, verse, chorus, bridge, solo, outro...) with its
tempo, kit and swing (`"swing"`: 0 straight, the default, up to 100, MPC's 50 to 75 %; every second step of a 1/4,
1/8, 1/16 or 1/32 grid comes late); 16 songs live in flash. The screens (the device and the editor) number
them 1..16; the pedal's PC and `fm1_song.py --put` count from 0 (song 1 is PC 0, `--put 0`). Sections the same:
the screens show 1..16, the value of CC 20 / 27 / 22 counts from 0 (section 1 is CC 20 value 0). Each section has a time signature (`"time"`, 4/4 if left out) and is
a whole number of its bars: the FM-1 counts bars, so a change waits for the bar line. A pickup or a short intro is a
short bar (4 steps of 1/16 is `"time": "1/4"`); `fm1_song.py` and the editor's section editor flag a section that
is not whole bars and offer the signature that fits. Write one as JSON (`tests/example-song.json` is the model; `songs/` is yours) and send it over USB:

```
python3 tools/fm1_song.py tests/example-song.json            # check it
python3 tools/fm1_song.py tests/example-song.json --put 0    # into song slot 0 (the FM-1 stopped)
python3 tools/fm1_song.py --pedal-channel 16            # the pedal's MIDI channel, kept through power-off
python3 tools/fm1_song.py --count-in 1                  # one bar of clicks before a start from stopped (0, 1, 2)
```

Its **Setlist** tab keeps the songs of a gig in the browser (offline in the installed app) and plays them there with
the real pedal (PC n picks song n of the set), with a **live mode** for a tablet on a music stand (full screen, big
section tiles, the screen kept awake); with the FM-1 connected it sends the set to the device's slots.

Or open the web editor (`web/editor.html`): its **Song** tab works without an FM-1 (the import, the section
editor and the player below); with the FM-1 connected by USB it also lists
the sixteen slots, takes a song JSON by drop or file picker, saves one back as JSON, erases a slot, and sets
the pedal's MIDI channel. Its **From Guitar Pro or JSON** card runs `tools/gp2song.py` itself in the browser (Python
through Pyodide, loaded from the internet the first time), or opens a song JSON as it is (one saved from the
editor, or from `songs/`); a file dropped on that card or on the editor opens too. Its **Play** card plays a song
with the kit's pads as the pedal would, on a view of the device's LIVE screen (the beat lights in the section's
time signature, the section with its bar and a progress ticked per bar, the count-in, what comes next, the sections
as tiles, all in time with what you hear): the tiles are the footswitches, with fills, the ending, stop, the song's
swing and the count-in as the device has them; keys (shown under the tiles): 1..8 a section (0 switches them to 9..16 and back), shift + the key on the next
bar, f a fill, comma and period previous / next, e the ending, space stop; **MIDI in from** and **Listen to the pedal** let the real pedal (the
Ampero, or a USB MIDI interface between it and the computer) drive that player with the same CCs and PCs as the
FM-1, on the Pedal card's channel (PC n opens song n of the FM-1 connected, or of the simulated one), so the
live workflow can be rehearsed before the hardware; **Follow its MIDI clock** makes the player take that input's
tempo and START / STOP, as the FM-1 does with SETUP > SYNC. Its **Section editor** card edits a song by hand: names, order,
length, the time signature, the outro, split and merge, the hits on a grid with levels, ratchets and fill marks, the fill
conditions, columns copied and pasted, undo and redo (Ctrl+Z / Ctrl+Y), keys for the pen (n h o g x, r for the
ratchet, m for the mark, [ ] between sections), a lane's sound on a click of its name, "Play this section" to
loop it, and, for a song read from Guitar Pro, the file's bars to pick as a section's pattern or its fill; then
Play, Send or Save JSON. The
Drum kit page opens with the Muldjord Kit on its pads (served from `web/kit/`, CC BY 4.0, see `docs/kits.md`), and
the player plays the device's layered kit MULDJORD (`web/realkit/`, its layers and takes as the device picks them),
or those pads once you edit them, or WAVs you pick. The page must be served, not opened as a file: from the project
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
on a PC. (With songs given, a song saved on the device in that run does not read back: the emulator shows the
flash image as loaded; with no songs given your slots are empty and it plays the factory grooves, which are read-only.)

```
python3 tools/fm1_emu.py                                                             # the demo on the factory grooves
python3 tools/fm1_emu.py songs/a.json songs/b.json --wav build/emu/run.wav         # your songs in slots 1 and 2
```

**Factory grooves.** The firmware carries the sixteen grooves of `grooves/` (`tools/gen_songs.py`, in the order of
`grooves/factory.txt`; `docs/grooves.md` describes each) as a bank of their own, apart from your 16 slots: the SONGS
screen's FACTORY page, the pedal's PC 16..31. They are read-only and always there, so a fresh device or the emulator
has something to play at once, and sending your songs never overwrites them. Your songs go in the USER slots: from the
web editor's Setlist tab (one slot, or the whole set: song n in slot n, the slots after it erased) or with
`fm1_song.py --put`. The Setlist tab lists the grooves too (they come with the site, so no FM-1 is needed): play one, open
it in the editor, or add it to the set to make it a song of your own.

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
The songs live in the flash of the USR1 and USR2 sample slots (8 songs each, two safe copies of every song), which
the drummer gives up: the kit pair USR3+4 stays yours. With no song loaded the
pedal plays SLOOP's own sections A..D (the four project slots). **Fills** are marks on the hits of a section's
pattern: a hit marked "only in a fill" plays only during a fill (CC 21, a black key or SEL on the LIVE screen),
one marked "not in a fill" rests then, mark by mark in the editor's grid or lane by lane in the JSON (`"fills"`);
SLOOP's own whole-step conditions still work on top. Built for the Hotone Ampero II Stage (each footswitch sends up to three CC / PC messages), but any
pedalboard with a MIDI out does.

## On the device

Four screens, a button each. Every one of them was checked on the fm1-emu emulator and in the host tests, not yet
on an FM-1.

| Button | Tapped | Held |
| --- | --- | --- |
| HOME | **LIVE**, the screen it starts on | |
| SAVE | **SONGS** (from SONGS: LIVE again) | on EDIT: the section into its song; elsewhere SLOOP's section layer (A..D) |
| SEQ | **EDIT**, the drum grid of the section playing (again: GRID / KIT) | the step layer, as SLOOP |
| GLO | **SETUP** (again, HOME or OCT-: closed) | |
| SEL / ENV / LFO | on LIVE: **FILL**, **PREV**, **NEXT** (the pedal's CC 21, 25, 24; PREV / NEXT without a fill) | |
| PLAY | start the section cued (the count-in first), or stop | |
| FX, EDIT, ARP | FX: the filter, insert, delay and reverb pages | SLOOP's layers (throws, erase / mute / undo, roll) |

**LIVE.** What plays, readable from the floor: the song and its slot, the tempo, the beat lights in the bar's
meter; the section in large letters with "BAR 3 / 8" and a progress ticked per bar ("COUNT 2 / 8" during the
count-in); what comes next, in colour (NEXT: CHORUS, FILL, FILL TO THE END, FILL > CHORUS for a fill into a change,
ENDING); the song's sections as tiles, four to a row as the white keys 1..16, as many rows as the song needs (up
to 4 sections the whole names, up to 8 a name each, 9..16 a line each: the number and the name cut; the one
playing lit, the one cued framed and blinking, the outro and any meter but 4/4 marked); and a footer with the kit, the pedal channel and a dot that lights when the pedal sends. The pedal's
messages ("SONG: BLACK", "NEXT: SOLO") show in the footer. The white keys 1..16 change section when the one
playing ends, with OCT+ held on the next bar (the state line says KEY: ON THE NEXT BAR), with no fill of their own; any black key asks for a fill to the end of the
pattern (as CC 21). So a plain change is one press, and a fill into the change is a black key then a white one,
as the pedal's CC 21 sent with its CC 20. SEL is a fill too, ENV and LFO the previous and next section
(without a fill). KNOB 1 (and SELECT) the tempo, KNOB 2 (and PRESETS) the kit, KNOB 3 the drums' level, KNOB 4 the
swing. With no song loaded it says PICK A SONG and shows the 16 songs of the SONGS page shown (USER or FACTORY;
ENV / LFO switch), and the white keys 1..16 load them.

**SONGS.** Two pages, as tabs in the header: **USER**, your 16 slots, and **FACTORY**, the 16 grooves (read-only). ENV
shows USER, LFO FACTORY (the header's "env/lfo: page"; "rec: load" at the bottom). It opens on the loaded song's page; with no song, on USER, or on FACTORY while your slots are
all empty. Each page lists 16 songs (9 rows at a time, the list scrolling with the row picked) with their name, tempo
and number of sections, and a last row JAM: SECTIONS A..D, SLOOP's four sections and their chain editor. KNOB 1 or
SELECT picks a row, REC or OCT+ loads it and goes back to LIVE; a white key 1..16 loads that song of the page at once.
The LIVE header names the song's bank: "song 3" or "factory 3". EDIT's SAVE held on a groove says FACTORY: READ-ONLY
(to change one, add it to the set in the web editor, edit it there and send it to a slot).

**EDIT.** SLOOP's drum grid and kit page for the section playing; its header names the section and, stopped,
the song. Change hits, levels and ratchets as SLOOP does, then hold SAVE (stopped): a ring fills for a second and
the section is written back into its song in flash ("SAVED INTO THE SONG"; "SONG FULL" past 3800 bytes; nudges and
parameter locks are not kept, SNG2 has no room for them, and the screen says so). With no song loaded SAVE held
saves the project slot, as SLOOP did. A tap of SAVE opens SONGS.

**SETUP.** SLOOP's menu, its GLO pages and its mix layer in one place, a section at a time (SELECT), up to four
rows each on KNOB 1..4 (PRESETS moves the cursor, OCT+ acts): PEDAL (channel, count-in, load the last song at
start, MIDI clock in: OFF, USB or TRS, the tempo and START / STOP from a clock master such as the Ampero), MIDI (the
drums' channel, IN, other channels, OUT), LEARN (MIDI LEARN, clear), TEMPO (tempo, click,
tune), FEEL (swing, humanize), DRUMS (level, reverb, delay, pan), MASTER, TEXTURE and TEXTURE 2, SCREEN, LIGHTS, AUDIO,
SYSTEM (calibration, the JAM project pages, ABOUT). The pedal settings are saved once the music stops.

**Humanize** (SETUP > FEEL: OFF, LIGHT, MORE; LIGHT out of the box): the sequencer plays each step a little early
or late (LIGHT up to 3 ms, MORE 6 ms; all the drums of a step together, so no flams) and each hit a little louder or
softer (up to about 1 and 2 dB), as a drummer does. The level moves after the hit's sample is picked, so an accent
or a soft hit keeps its own sound. It varies from bar to bar but is the same every time the song plays, so a render
is repeatable. The web editor's player does the same (its Humanize menu), and `tools/fm1_render.py --humanize`.

What else is on it, from SLOOP: one track, the drums (the kits: the sampled GM kit and its treatments, the
synthesised kits, SYN1..SYN4 from the editor's Drum synth, your own sampled kits on the user slots; and this fork's
MULDJORD), a pattern of
up to 64 steps over 16 lanes with levels, ratchets, nudges, parameter locks and fill conditions, rolls, erase and
mutes; sections A..D, their quick chain and SLOOP's song order; the drum track's INSERT and FILTER, the delay and
reverb sends, the master (DUST, DUCK, the DJ filter, the limiter), TEXTURE; MIDI in, out, clock and learn. The web
editor (protocol v11) edits the pattern, the kits, the drum synth, the samples, projects, songs and settings. (Gone
from SLOOP's screens: TRACKS, the visualiser, the GLO pages and layer, the HOME-held menu.)

## Build and test

```
./build.sh                   # build/felucca.bin, build/felucca.fwsc (toolchain and SDK: BUILDING.md)
sh tests/run_tests.sh        # the host tests (SOAK_MIN=1 for a short soak)
```

The TRS MIDI input is SLOOP's (`FELUCCA_UART=1`, in use since SLOOP 2.3). Install it as any Felucca
firmware ([BUILDING.md](BUILDING.md)): at your own risk.

## Layout

| Path | What |
| --- | --- |
| `docs/design.md` | the model, the pedal command map, milestones, flash budget, review notes, open questions |
| `docs/drums-only.md` | how SLOOP's synth side was taken out, stage by stage, and what each stage kept |
| `docs/ampero-setup.md` | what to put into the Ampero II Stage's footswitches, the cable, what to check with the FM-1 |
| `docs/kits.md` | the drum kits: the chosen Muldjord Kit (CC BY 4.0, with Black Pearl's hats and Red Zeppelin's ride), the others tried, the recipes, the slot budget |
| `firmware/src/pedal.c` | the pedal: CC / PC from the MIDI in queue to section, song and transport requests |
| `firmware/src/sng.c` | songs: the packed form ("SNG2": a byte a hit, a meter per section), read in place from flash, a section into the drum track on the bar, the pedal settings |
| `firmware/src/ui_drummer.c` | LIVE and SONGS (on SLOOP's SONG page): the song playing, its sections, the keys as the pedal; the song list |
| `firmware/src/ui_menu.c` | SETUP: the pedal, MIDI, tempo, drums, master, texture, screen and system settings |
| `firmware/src/ui_studio.c`, `ui_input.c` | EDIT (SLOOP's drum grid and kit pages); the buttons, the layers and SAVE's hold on the grid |
| `tools/fm1_emu.py` | the firmware on the fm1-emu emulator, songs imported into its flash: a script of keys, knobs and pedal MIDI, screens and sound saved |
| `tools/fm1_song.py`, `songs/` | songs from JSON: check, pack, send, read back; `songs/` holds your songs (sent to the device or played from the setlist; the factory songs are the grooves) |
| `grooves/`, `docs/grooves.md` | generic grooves, not songs (two sections, verse and chorus, 4-bar phrases, a fill at each phrase's end; each one's tempo, meter, verse, chorus and fill in `docs/grooves.md`): classic rock, blues, rock ballad, slow rock, punk, half-time shuffle, train beat, one drop, four on the floor, metal gallop, a 6/8 ballad, and some to spark ideas (7/8, a tom groove, linear funk, three over four, ride and kick only); the firmware's factory songs, slots 1..16 in `grooves/factory.txt`'s order |
| `web/editor.html` | the web editor (SLOOP's, drums only, protocol v11) with a Song tab (no FM-1 needed but to send and set the pedal): the pedal channel, Guitar Pro import (Pyodide), the section editor, a player; a Setlist tab: the device's sixteen slots; `web/test_web.mjs` tests it against the simulated FM-1 |
| `web/kit/` | the kit the editor serves (the Muldjord Kit with two velocity layers), the files `tools/mkkit.py` makes the device image from |
| `tools/gen_realkit.py`, `tools/realkit.json`, `assets/realkit/` | the built-in layered kit MULDJORD: the recipe, rendered once from the source kits into WAVs (layers, takes, levels, fitted to 290 KB); `tools/gen_samples.py` builds them in as set REAL, the editor's player plays them (`web/realkit/`) |
| `tools/fm1_render.py`, `tests/song_render.c` | a song played by the pedal, rendered to a WAV on the host |
| `tools/gp2song.py`, `tests/gp2song_test.py` | the drum track of a Guitar Pro file into a song (markers, patterns, fills, levels, ratchets) |
| `tools/mkkit.py` | a sampled kit from a folder of WAVs or an SFZ kit onto USR3+4: lanes by name, two velocity layers, trim, cut, level; an image for the renderer, or the upload |
| `firmware/src/seq.c` | SLOOP's sequencer; the pedal hooks in its MIDI loop and `events_block` |
| `tests/pedal_test.c` | the pedal through the real sequencer: sections on the bar, fills, endings, next / previous, stop, tap |
| `tests/song_test.c` | songs through the real sequencer: the packed form's checks, PC, sections, the outro, back to A..D, a section packed again from the drum track (EDIT's save) |
| `tests/ui_pages_test.c` | the device's UI on a framebuffer: LIVE, SONGS, EDIT, SETUP, the layers, renders of every screen |
| everything else | SLOOP 2.6's drum side as forked |

## Licence

GPL-3.0-only, as SLOOP (see [LICENSE](LICENSE) and [LICENSING.md](LICENSING.md) for the assets).
M-VAVE and FM-1 are trademarks of their owners; this project is not affiliated with M-VAVE or Hotone.
