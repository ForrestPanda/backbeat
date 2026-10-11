# fm1-arranger

A guitarist's accompaniment device on the M-VAVE FM-1: songs made of drum sections (intro, verse,
chorus, bridge, solo, outro), switched live from a pedalboard over the TRS MIDI jack, with fills on
the transitions. No backing tracks: the song's structure and the length of every section are decided
on stage.

A fork of [SLOOP](https://github.com/isod89/sloop-fm1) 2.6 (GPL-3.0, itself from
[Felucca](https://github.com/hugelton/Felucca)). SLOOP's manual is [SLOOP.md](SLOOP.md); everything
it describes still works. What this fork adds is in [docs/design.md](docs/design.md).

Status: **milestone 3, no hardware yet**. Nothing here has run on an FM-1. The firmware builds and
packages; the pedal control passes the host tests.

## The pedal

Footswitches send MIDI on channel 16 (default) to the TRS jack or over USB:

| Message | Meaning |
| --- | --- |
| CC 20, value 0..3 | section A..D on the next bar, the rest of this bar a fill; stopped: start with it |
| CC 27, value 0..3 | the same with a whole fill bar first |
| CC 21 | fill the next bar, stay in the section |
| CC 22, value 0..3 | ending: the fill, that section once as the outro, stop on the bar (other values: stop on the next bar) |
| CC 23 | stop now |
| CC 24 / CC 25 | next / previous section (empty ones skipped), with the fill |
| CC 26 | tap tempo |
| PC 0..7 | load that song: stopped, its tempo and kit now and its first section cued; playing, with the next section |
| PC 8..127 | no song: SLOOP's sections A..D again |

**On the screen.** The TRACKS screen shows the section playing and its bar in the title's place; the
pedal's requests are said in the top bar ("NEXT: CHORUS", "SONG: BLACK", "ENDING: OUTRO").

**Songs.** A song is up to 8 named drum sections (intro, verse, chorus, bridge, solo, outro...) with its
tempo, kit and swing; 8 songs live in flash. Write one as JSON (`songs/example.json`) and send it over USB:

```
python3 tools/fm1_song.py songs/example.json            # check it
python3 tools/fm1_song.py songs/example.json --put 0    # into song slot 0 (the FM-1 stopped)
python3 tools/fm1_song.py --pedal-channel 16            # the pedal's MIDI channel, kept through power-off
```

Or take the drum track of a Guitar Pro file (.gp3 / .gp4 / .gp5; `pip install pyguitarpro`): the score's
section markers become the sections, each with the pattern its bars repeat and the bar that deviates as
the fill; edit the JSON afterwards:

```
python3 tools/gp2song.py "song.gp4" --out songs/song.json
```

Then PC 0 from the pedal loads it and CC 20 picks its sections (0 = the first). Hear it on the computer
first, played by the pedal through the firmware's own drum engine (after a `./build.sh`):

```
python3 tools/fm1_render.py songs/example.json --play "INTRO:2 VERSE:8 CHORUS:8 SOLO:8 OUTRO" --listen
```

**A sampled kit of your own.** A folder of WAV files (any CC0 kit; `assets/samples-cc0/KIT` is one)
becomes the device's kit USR3+4: the files are sorted onto the 16 lanes by name, trimmed, cut to the
slots' 2 x 7.4 s and levelled:

```
python3 tools/mkkit.py ROCK path/to/wavs --image build/host/rock.img     # hear it: fm1_render.py --usrkit build/host/rock.img
python3 tools/mkkit.py ROCK path/to/wavs --load                           # into the FM-1 (USR3 and USR4)
```

Songs then use that kit by its index (`"kit": 41` in the JSON, the kit after the built-in and SYN kits).
The songs live in the flash of the USR1 slot, which the arranger gives up (USR2 stays free). With no song loaded the
pedal plays SLOOP's own sections A..D (the four project slots). A fill is SLOOP's: the steps of the pattern marked FILL ONLY play only then, NO FILL steps rest (a step
held + OCT+, or the web editor). Built for the Hotone Ampero II Stage (each footswitch sends up to three CC / PC messages), but any
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
| `firmware/src/pedal.c` | the pedal: CC / PC from the MIDI in queue to section, song and transport requests |
| `firmware/src/sng.c` | songs: the packed form, read in place from flash, a section into the drum track on the bar, the pedal settings |
| `tools/fm1_song.py`, `songs/` | songs from JSON: check, pack, send, read back; the example and four from Guitar Pro tabs (Black, Jeremy, Another Brick in the Wall, Comfortably Numb) |
| `tools/fm1_render.py`, `tests/song_render.c` | a song played by the pedal, rendered to a WAV on the host |
| `tools/gp2song.py`, `tests/gp2song_test.py` | the drum track of a Guitar Pro file into a song (markers, patterns, fills, levels, ratchets) |
| `tools/mkkit.py` | a sampled kit from a folder of WAVs onto USR3+4: lanes by name, trim, cut, level; an image for the renderer, or the upload |
| `firmware/src/seq.c` | SLOOP's sequencer; the pedal hooks in its MIDI loop and `events_block` |
| `tests/pedal_test.c` | the pedal through the real sequencer: sections on the bar, fills, endings, next / previous, stop, tap |
| `tests/song_test.c` | songs through the real sequencer: the packed form's checks, PC, sections, the outro, back to A..D |
| everything else | SLOOP 2.6 as forked |

## Licence

GPL-3.0-only, as SLOOP (see [LICENSE](LICENSE) and [LICENSING.md](LICENSING.md) for the assets).
M-VAVE and FM-1 are trademarks of their owners; this project is not affiliated with M-VAVE or Hotone.
