# BackBeat (fm1-drummer): design

A guitarist's accompaniment device on the M-VAVE FM-1: a drummer who knows the song's parts and takes
cues from a footswitch (named **BackBeat** 2026-10-14; it was fm1-arranger, then fm1-drummer, the name the repository
keeps). The idea comes from an arranger keyboard,
reduced to what a guitarist with a pedalboard needs: songs made of sections (intro, verse, chorus,
bridge, solo, outro), each a drum loop, switched live from the pedalboard, with fills where the
player asks for them (a footswitch can send the fill with the change). No backing tracks: the structure and the length of every section, the solo above all,
are decided on stage.

Base: a fork of [SLOOP](https://github.com/isod89/sloop-fm1) (GPL-3, from Felucca), rebased on
**SLOOP 2.6** on 2026-10-11 (first done on 2.2). SLOOP brings the sample-accurate clock, the drum engine
(synthesised kits, a drum synth, a sampled acoustic kit, your own 16-pad WAV kits from the editor), an
ADPCM sample engine, live sections that switch on the bar, a quick chain, **fills** (per-step
FILL ONLY / NO FILL conditions, "fill while held" and "fill the next bar"), the TRS MIDI jack (driver
fixed and used for MIDI clock since 2.3), per-track MIDI channels, Program Change, MIDI import and
export in the web editor, and the installer. Since 2026-10-14 the fork is drums only (`docs/drums-only.md`):
SLOOP's synth side (its engines, presets, sampled instruments, punch FX, SLICER, live recording, arpeggiator,
scales and chords, three of its four tracks) is out, and with it 332 KB of flash for the drums.

## The pedalboard

A Hotone Ampero II Stage. Its MIDI OUT is a 3.5 mm TRS jack, Type A (Hotone's quickstart). The FM-1's
TRS MIDI IN is Type A too: a plain stereo 3.5 mm cable from the Ampero works (verified 2026-10-19).

Each Ampero footswitch sends up to three MIDI messages (CC 0..127 or PC, with a value, on a channel),
only in Stomp mode and only when the switch's own function is Effect Slot, Scene or OFF. Scenes are the
natural pairing: the scene that switches to the lead sound also sends "go to the solo section".

**Only from the TRS jack** (2026-10-13; the Stage's manual for firmware V1.7.0, and the Ampero on the
user's computer): Footswitch MIDI and Patch MIDI go out "via the MIDI OUT/THRU jack" only. Its USB MIDI port
sent only Hotone's own SysEx (patch dumps, `F0 21 25 4D 50 ..`), no CC or PC; USB and Bluetooth MIDI are
documented as inputs (MIDI IN SOURCE) and clock out. On stage that is the design (Ampero TRS out -> FM-1 TRS
in); to try the footswitches on the computer (`aseqdump`, the editor's MIDI in from) a USB MIDI interface on the
Ampero's TRS MIDI OUT is needed. (Linux here: BlueZ 5.64 without its MIDI plugin, so the Ampero's Bluetooth
MIDI service gives no ALSA port either.)

**Verify on the Ampero:** whether a switch assigned to an effect slot sends its messages on both the
on-press and the off-press, and whether a patch change sends a program change on a global channel.
The command map is built so that neither matters.

## Command map (firmware/src/pedal.c)

One channel for the pedal, default 16 (0 = off, 17 = omni). Everything is absolute, so a switch that
repeats its message does no harm; next / previous / tap count presses.

| Message | Meaning | Status |
| --- | --- | --- |
| CC 20, value n | go to section n when the section playing has played its pattern out (its bars, counted from where it started), no fill of its own; a 1-bar section: the next bar; a fill pending (asked before or after): on the bar after the fill instead; stopped: start the loop with it. n the section playing, nothing cued: nothing; with an ending pending it cancels the ending, as any section | done (n = 0..3, A..D; at the section's end 2026-10-13; no fill 2026-10-14; after a fill 2026-10-15) |
| CC 27, value n | on the next bar, without waiting for the section's end, no fill of its own (a fill pending: after it) | done (no fill bar 2026-10-14) |
| CC 28, value n | the break: section n through its break (bars written to play before its pattern, below), when the section playing ends, as CC 20 (a fill pending: after it, so one switch can send the fill and the break); the section playing too (it comes in again through its break). n not a section (127): the section playing's (stopped: the one cued, else the first). No break written: its entry; neither: a bar of rest. Stopped: the loop starts with it. CC 20, 22, 24, 25, 27 bring a section in through its **entry**, if written, from another section or from stopped; the section playing asked for again stays a no-op | done 2026-10-18 (firmware, the browser player, the section editor) |
| CC 21, value v | the fill. A section with a fill bar of its own, as an arranger's fill-in: from now to the end of this bar (the fill bar instead of the pattern's, its step i at the bar's step i), then the pattern again from its top on the next bar, or the section asked for. A section without one: fill mode from now to the section's end (the FILL ONLY hits in, the NO FILL hits out, where they sit; a section with no fill at all: nothing changes), then the pattern on from its top, no restart, or the section asked for at that end. Asked for in the bar's last beat: the whole next bar (no fill bar, in the section's last bar: the whole next cycle). Value 1: the whole next bar from anywhere in this one (a fill bar whose hits lead off the bar); in a fill, the next bar a fill too. Any other value: from now. Sent with a section change (the same footswitch, either order): the fill into it, the change on the bar after the fill | done (to the pattern's end 2026-10-13; the arrangers' way 2026-10-15; value 1 2026-10-16; to the section's end without a fill bar 2026-10-17) |
| CC 22, value n | ending: the section playing ends (as CC 20), section n once as the outro, stop on the bar; n empty or out of range: the section ends, then stop | done (at the section's end 2026-10-13) |
| CC 23 | stop now | done |
| CC 24 / CC 25 | next / previous section, empty ones skipped, wrapping, as CC 20; stopped (nothing cued): the first / the last | done |
| CC 26 | tap tempo | done |
| PC n | load song n: 0..15 the user's slots (0..7 until 2026-10-14), 16..31 the factory grooves (since 2026-10-15); 32..127: no song, A..D again | done |
| (setting) count-in 0, 1, 2 bars | a section from stopped: that many bars of clicks at the tempo first (the metronome's voice, accents on the bar), then the loop starts with it; CC 23 cancels, another section meanwhile only changes what starts; playing: unchanged | done 2026-10-12 |

The count-in rides on SLOOP's REC count-in (`ci_on`, `events_block`): `ci_pedal` holds its beats, so a
MIDI START or STOP cuts it as SLOOP's. Kept in the pedal settings object (`arrset_t.count_in`), set by
`tools/fm1_song.py --count-in`, the editor's pedal row, heard with `fm1_render.py --count-in`.

Program change as song select lets one Ampero patch be one song: the patch change loads the song, the
switches inside the patch are its sections. Starting a song is "go to intro".

## The model (target)

- A **song** (done in M3): a name, a tempo, a kit, swing, drum level and reverb, up to 8 **sections**.
  Sixteen songs on the device (eight until 2026-10-14: a set needs more).
- A **section** (M3): a name, a drum pattern (1 to 64 steps at any of SLOOP's divisions, 16 lanes with a
  level, a ratchet and, since 2026-10-17, a flam per hit) with its fill conditions (FILL ONLY / NO FILL steps: the fill is inside
  the pattern, not a second one), and the outro flag.
- **Why a section is at most 4 bars** (asked 2026-10-17). Because a section is a pattern, and a pattern is at most
  64 steps. Why that is 4 bars: at 1/16
  in 4/4, a bar is 16 steps, so 64 steps is 4 bars. The limit moves with the division and the meter: 8 bars at 1/8,
  4 at 1/16, 5 at 8T (12 steps a bar), 2 at 16T (24 steps a bar), 2 at 1/32. That is also why the Guitar Pro
  importer looks for a repeating period of 1, 2 or 4 bars, and only 2 at 1/32. Why it is enough by design: a
  section is a groove that repeats, not a transcription. A verse that is 8 bars of the same 2-bar beat is a 2-bar
  section that loops until the pedal says otherwise. The things that happen inside a part, the fill and the change,
  are the pedal's: the fill bar and the next section. Material that never repeats, 16 different bars in a row, is a
  drum track rather than a drummer, and the device's RAM and flash were not spent on that. If a song really has a
  part with more than 4 distinct bars at 1/16, the way through is to split it into two sections and step through
  them with the pedal, or with next-section cues, and the song keeps up to 16 sections for that.
- **Swing** (2026-10-14): a song's `"swing"` is SLOOP's 0..100 (0 straight, 100 MPC 75 %: `seq.c swing_units`, every
  second step of a straight grid that much late). The importer, `fm1_song.py` and the editor wrote 50 (MPC's "50 %",
  straight on the old scale), which the device played as a shuffle (MPC 62.5 %) while the web player ignored swing:
  the default is 0 now, the songs were set to 0, and the web player swings as the device does.
  (2026-10-19: "I think the swing option might be broken".) It was not, but it moved the wrong steps for every song on the
  bench: all at 1/16 with the hats on the 8ths, so hardly a hit on an odd 16th, and the swing had nothing to move; the
  user's case was Money, whose score is marked with the shuffle (two 8ths as a triplet's quarter and 8th, Guitar Pro's
  eighth triplet feel on every measure). Now a song's swing has a grid, `"swing_grid"`: `"1/16"` (the default: every
  second step of the section's grid, as before) or `"1/8"` (the 8ths go long-short and the 16ths or 32nds inside each
  share its length evenly: `seq.c grid_den8`, chosen by `trk_grid` under 1/16 and 1/32 when the song says so; under
  1/8 it is the plain swing, and 1/4 and the triplet grids are untouched). The packed header's first spare byte is a
  flags byte, bit 0 the 8ths (`SNG_H_SWING8`; older firmware reads it as reserved and swings the 16ths). The web player
  does the same (`PLAY.tick`, the grid kept with the old song's swing when a song is loaded while playing); the section
  editor has the grid next to the swing, and both follow live into the song playing below (before, a swing edited while
  playing waited for the next Play); `fm1_song.py` packs and describes it; `gp2song.py` sets swing 67 (2:1, MPC 66.7 %)
  from a triplet feel on most measures, on the 8ths for an eighth feel. Tests: seq2_test t_swing8 (16ths 3:3:1:1 in
  each beat at swing 100), song_test (the flag read), test_web.mjs (the player's times, the packers, the editor).
  Not on the device yet: the FM-1 runs the bridge; the next BackBeat install carries it.
- A section is a whole number of bars of its time signature (2026-10-14, asked when Back in Black's 4-step
  intro in 4/4 looped four times before the verse: the signature rules, the firmware keeps counting bars).
  `tools/fm1_song.py` (`bar_fit`, `tests/song_bars_test.py`) and the editor's section editor (`SED.barFit`) flag one
  that is not and offer the signature whose one bar is the pattern ("1/4" for that intro). A count-in before such a
  section clicks its short bars (one click a bar for "1/4"): to revisit if it feels wrong.
- A **transition** (done in M2; changed 2026-10-13 and 2026-10-14): a section request lets the section playing
  finish its pattern (a 4-bar verse plays its four bars); a 1-bar section switches on the next bar. No fill
  comes with it (asked 2026-10-14: "I don't want to force a fill on every transition"; until then the last
  bar was the fill, and CC 27 put a whole fill bar first): the fill is CC 21's, and a footswitch that wants
  one sends CC 21 with the section, the change then landing on the bar after the fill. `pedal_at_end` in seq.c: `live_bar` counts the bars
  since the section started, the pattern's bars (`sng_bars` / `section_bars`) are the period. CC 27 does
  not wait: it lands on the next bar, for a change that must come now. The ending (CC 22) waits for the section's end the same way, then the outro once,
  then a stop on the bar (no outro: the section's end, then the stop). SLOOP's own section keys and
  the chain still switch on the next bar: only the pedal waits. A fill request (CC 21) is bar-aligned since
  2026-10-15 (see "Fills as the arrangers do them" below; until then it ran to the pattern's end, and from
  the pattern's last bar through the next cycle). SLOOP's own next-bar fill (`fill_arm`, GLO + key 10) is left to its keys.
- **Bass: dropped** (deferred 2026-10-11, dropped from the plans 2026-10-13: drums only). If ever wanted,
  it costs little: a section already stores SLOOP's synth tracks, so a bass pattern per section with a
  bass sound works without new code; the chord changes would be written into the pattern, since a
  guitarist cannot feed chords live as an arranger keyboard's left hand does.

What SLOOP 2.6 has today: 4 sections A..D that double as the 4 project slots, each a full project
(3 synth tracks + drums + kit + globals); a section switch on the next bar; a quick chain of up to 8
and a song chain of up to 16 steps; fills as step conditions inside a pattern (a FILL ONLY step plays
only during a fill, a NO FILL step is silent then), called by GLO + key 9 (while held) or key 10 (the
next bar). No named sections, one song, no fill tied to a section change.

## Milestones

1. **Pedal to section (done, host-tested).** CC / PC from the TRS jack or USB reach a `pedal_msg`
   dispatcher in the sequencer's MIDI loop; sections A..D, next / previous, stop, tap tempo. The TRS
   input is built in (`FELUCCA_UART=1`), untested on hardware as in SLOOP. Test: `tests/pedal_test.c`.
2. **Fills and endings (done, host-tested).** On SLOOP's fill conditions: a section request from the
   pedal sets `pedal_fill` for the rest of the bar (`fill_now` takes it with the UI's fill flags; since
   2026-10-13 for the section's last bar, the request waiting for the section's end), CC 27
   waits one whole fill bar, CC 21 arms SLOOP's next-bar fill, CC 22 plays the outro for its bars (as the
   quick chain counts them) and stops on the bar. The outro is whichever section the pedal names: no
   outro flag until sections are named in M3. Still 4 sections, still SLOOP's project slots.
3. **Songs (done, host-tested).** `firmware/src/sng.c`: a packed song (header + up to 8 sections: name,
   length, division, flags, fill conditions, drum steps) in one flash sector per slot, 8 slots with A/B
   copies at 0xA0000..0xAFFFF (the USR1 sample slot's flash, which the drummer gives up: sample uploads
   into USR1 are refused; first put at 0xE7000, which turned out to be USR4's), read in place through XIP
   as the FM6 bank is (static RAM was 5.5 KB from full). PC n loads a song: stopped, tempo, kit and the first section at once; playing, with the next
   section on the bar. While a song is loaded the pedal's sections are the song's; a section is the drum
   track only (the synth tracks keep theirs). The ending without a section takes the flagged outro. The
   pedal channel is a 16-byte object of its own (0xB0000 / 0xB1000), set by the tool; no menu page yet.
   A song may be up to 3800 bytes (a sector's payload; the importer shortens patterns to fit). Since 2026-10-13
   the packed form is "SNG2" (3b below): a byte a hit, so 8 sections of 64 steps fit.
   Songs are written as JSON and sent with `tools/fm1_song.py` over the editor protocol's backup PUT
   (objects 16..23, 24). SLOOP's own sections A..D and song screen are untouched and come back with PC 127 (any PC from 32).
4. **Hearing it, and the kit (reshaped on 2026-10-11: the cheap way).** The flash audit (below) showed
   that stripping SLOOP buys little except by dropping its sampled instruments, and that songs and user
   kits have their own flash anyway. So:
   - (done) `tools/fm1_render.py` / `tests/song_render.c`: a song played by the pedal, rendered to a
     WAV by the firmware's own engine, with the transitions and fills, any kit.
   - One rock kit (the user plays rock): first SLOOP's built-in ACOUSTIC kit and its treatments, judged
     with the renderer; then a CC0 rock kit as 16 WAVs on the paired user slot (USR3+4, about 15 s at
     22 kHz ADPCM) through SLOOP's own kit upload. (done, first version) `tools/mkkit.py`: a folder of
     WAVs onto the lanes by name (the word nearest the start of the file name, with second-choice
     words), the onset found, the tail cut at a per-lane length with a fade and shortened if a slot
     overflows, a per-lane peak gain, USR3 for KICK..RIM and USR4 for SNARE2..COWBELL (the device looks
     in USR3 first); outputs an image for the renderer (`fm1_render.py --usrkit`), the processed WAVs
     for the web editor, the upload tool's files, or the upload itself. Levels are peak-based for now
     (SLOOP's own kits are loudness-matched with `tools/level_drumkits.py`): judge by ear with the
     renderer and adjust with `--gain`. No firmware change. The kit index is 41 (KIT USR3+4) in a song.
     (later) The kit chosen by ear is the Muldjord Kit (CC BY 4.0; `docs/kits.md`), with two velocity
     layers after all: USR3 the hard hits, USR4 the soft ones and the one-layer lanes, the device looking
     in the other slot for a lane it misses (`drums.c`), and a sample rate per zone (hats and rim at
     44.1 kHz). The second slot is the tight one: `mkkit.py` keeps the cymbals whole with per-lane floors.
   - Stripping code only if a second built-in kit or a bigger feature needs the room.
5a. **The editor's Songs tab (done 2026-10-11).** SLOOP's web editor gained a Songs tab: the eight
   slots with name, tempo and section names (read from the headers through BK_GET, after a BK_LIST),
   a song JSON sent by drop or file picker (packed in the browser, byte for byte as the Python tool),
   saved back as JSON (unpacked from the device), erased, and the pedal channel. The simulated FM-1 in
   the editor holds songs too, so `web/test_web.mjs` round-trips one. (later the same day) **From Guitar Pro** in the tab: `tools/gp2song.py`
   unchanged, run in the browser by Pyodide (Python in WebAssembly, about 10 MB from a CDN on first
   use; PyGuitarPro from PyPI through micropip; the importer's `convert()` is what both the CLI and
   the page call, so the result is identical), with the sections and auto options, the importer's
   log, Send to a slot, Save JSON, Play. **Play**: the song through Web Audio with the Drum kit page's
   pads (as they would go to the device) or the processed WAVs of `tools/mkkit.py --wavs`; the
   section buttons and keys 1..8 are the pedal, with the wait for the section's end and its fill bar, CC 27 (shift), the next-bar
   fill, the ending and stop, as a state machine (`PLAY`) tested in `web/test_web.mjs`. The transport (since 2026-10-15,
   for a tablet): Previous / Fill / Next in one row, Play / Stop / Ending under them, a Play button being the device's
   PLAY key while stopped (the section cued, else the first, the count-in too; key p), Play and Stop greyed by the state.
   The tempo in the head (KNOB 1 on the device): - / + a BPM, the number typed, Tap (as the pedal's CC 26; keys - + t),
   40..240, from the next step; greyed while a MIDI clock drives the player.
   The page lays itself out for three sizes (the phone layer 2026-10-16): the desktop's side bar above 860 px, a top
   bar on a tablet under it, and a phone layer at 600 px and under (live mode on one screen, the pedal's rows with
   whole words, the grooves and the count-in row away; the grid's cells at 26 px with the lane names sticky).
   test_web checks the rules are there; the layouts themselves are checked by eye on the user's tablet and phone
   over adb (the future-work entry "A responsive site" has the method and what is still open).
   No Web MIDI: the status says whether the browser has none, or the page is not a secure context (http:// from another
   machine, a downloaded file opened as content:// on Android): Chrome hides the API there. A preview:
   no ADPCM, no limiter, no reverb; `tools/fm1_render.py` stays the reference. (Since 2026-10-14: no fill comes with
   a section; the card shows the device's LIVE screen in time with the sound, from the steps the audio clock has
   reached: beat lights, the bar and its progress, the count-in, what comes next, the sections as tiles; the keys
   read the key, so shift + 1..8 and the keypad work, and are listed under the tiles; the song's swing is applied as
   the device does; the ending while the outro plays stops at its end.)
5b. **The section editor (done 2026-10-11).** A card on the Songs tab over the working song (from the
   Guitar Pro import, a JSON file, a slot's Edit, or a new song): the song's name, tempo, kit and
   swing; the sections as chips; for the selected one its name, steps, division, the outro flag, up
   and down, duplicate, new, delete, split at a bar, merge with the next; a grid of 16 lanes by the
   steps with a pen (normal, hard, soft, ghost, erase) and a ratchet, bar and beat lines, and a top row
   for the fill conditions; for a song from Guitar Pro the file's bars as a strip (the importer returns
   every bar as strings at 1/16 and each section's range), 1 to 4 bars picked as the pattern, one as
   the fill by the importer's rule; Play, Send to a slot, Save JSON. The operations are a pure module
   (`SED`: norm, setHit, setCond, setSteps, split, merge, fromBars, withFill...) tested in
   `web/test_web.mjs`; the page only draws and calls them. (2026-10-21) "Keep the song in the editor" (the
   import card, on by default): with a song in the editor a Guitar Pro or MIDI file brings only its bars and
   ranges, a song JSON keeps the bars there, so a saved JSON and its file are worked on together (the saved
   JSON carries no bars). (2026-10-12) The import card opens a song JSON
   too, by picker or by a drop on it or on the editor; undo / redo on a history of JSON states (`SED.hist`,
   `mark` after every render, so a redraw is told from a change), keys for the pen, ratchet, flam, mark and
   sections, Delete / Ctrl+C / Ctrl+V on the selected columns, a lane's sound on its name, "Play this
   section"; the player counts in as the device's setting (mirrored from the pedal row, or picked on the
   Play card), with a synthesised click.
5. **Authoring from Guitar Pro (decided and done 2026-10-11, first version).** The user writes songs in
   Guitar Pro 3 / 4. `tools/gp2song.py` (PyGuitarPro) takes the drum track of a .gp3 / .gp4: the measures between the
   score's section markers (Intro, Verse, Chorus...) become the sections, GM drum notes become the 16
   lanes (kick 35/36, snare 38/40, side stick 37 -> RIM, hats 42/44/46, toms 41..50, crash 49/57, ride
   51/59, cowbell 56, shaker 70, congas 62..64 (left out since 2026-10-14: the mid tom has their lane), clap 39), velocities become the four levels, a fill
   marked in the last bar (or the bar before a marker) becomes FILL ONLY steps, at 1/16 (or 1/32 when
   the bar needs it), sections up to 64 steps (the drum pattern repeats inside longer sections if it
   is the same). Later, other tracks of the file (the bass line) when bass comes back. The JSON stays
   the intermediate form, editable by hand.
   What the first version does: every percussion track merged; the pattern = the 1, 2, 3 or 4 bars
   (the most common bar at each position) that most of the section repeats, 1/32 only when an eighth
   of the hits sit between 16ths; the fill = the section's last bar when it deviates, else the most
   common deviating bar at phrase ends when the match is strong; sections with the same pattern as an
   earlier one skipped; at most 8, the outro kept. A limit worth knowing: a step's fill condition
   covers all its lanes, so a fill hit that lands on a step where the pattern also hits is dropped
   (listed by the tool): fills come out "additive". Tested on Pearl Jam and Pink Floyd tabs of the
   user's, and by `tests/gp2song_test.py` on a file written with PyGuitarPro. A file without markers
   (Shine On You Crazy Diamond, 348 bars): the sections are found from the drums alone, as the runs of
   bars that repeat a pattern of 1 to 4 bars with fills allowed; silence is dropped, short runs join
   their neighbour, equal neighbours merge; they are named PART n for the user to rename (`--auto`
   does the same for a file with markers).
3b. **Compact songs and a time signature per section (done 2026-10-13).** The packed form is "SNG2": a section
   header of 18 bytes (name, steps, division, flags, the meter, the section's size), the fill conditions, then a
   step as 2 bytes of lanes and a byte a hit (level, ratchet, fill mark; bit 6 a flam since 2026-10-17). A 64-step section of three hits a step
   takes about 340 bytes instead of 928; the user's songs shrank 3 to 5 times (TIME, 4336 bytes in SNG1, is 856).
   SNG1 is not read: no device ever held one. The meter byte (0 = 4/4, else n 1..16 and d 2, 4, 8, 16) sets the
   bar the pedal counts while the section plays (seq.c `clk_meter`): bars are counted in 16ths from the
   section's start, so a 7/8 bar line falls inside a beat and the section after it starts there
   (`seq_reset_tracks` gets the remainder past it); the armed fill bar, the ending, the click (a 1/den note, the
   bar's first accented) and the pedal's count-in (`count` bars of the cued section's meter) follow it; SLOOP's
   A..D stay 4/4. `"time": "7/8"` in the JSON; gp2song.py takes it from the pattern bars' time signature (and
   gives each bar's to the editor's bar strip); the section editor has a time field, draws its bars and beats
   and splits on them; the browser player counts in 1/48 of a beat, so a triplet division in an odd meter has
   its bar lines inside steps, as the device. Renders of the songs in 4/4 are byte-identical to SNG1's.
   Tests: song_test.c (a 7/8 section: its bar lines, the click's eighths, the count-in), test_web.mjs (the
   packers equal, the meter, the player, the editor), gp2song_test.py (a 7/8 file).
5c. **The real pedal into the editor (done 2026-10-13).** The Play card's **MIDI in from** (every input but the
   FM-1's, through a Web MIDI access of its own without SysEx, so it works beside the simulated FM-1 too) and
   **Listen to the pedal** (the click that lets the browser sound). `PEDALIN.route` turns CC 20..27 and PC on the
   Pedal card's channel into the Play card's actions, `PEDALIN.say` logs them in the FM-1's words, `PEDALIN.tap`
   is pedal_tap; PC n opens slot n of the FM-1 connected, which `PLAY.load` takes as the device does (stopped:
   cued; playing: the pattern goes on, the new song's sections from the next one asked for, its tempo with it).
   The router and the player are unit-tested; the port side is still to be tried by hand, through a USB MIDI
   interface on the Ampero's TRS out (the Ampero sends its footswitch MIDI from that jack only: "The pedalboard").
5d. **The pedal over Bluetooth (done 2026-10-19).** The Play card's **Connect Bluetooth**: the page is a BLE central
   (Web Bluetooth, Chrome and Edge on Android, Windows, macOS and Linux, from https or localhost; Safari on the iPhone
   has neither it nor Web MIDI; Chrome on Linux keeps it behind a flag, so the button is always there and says why
   when pressed without the API: hidden, it was not found, 2026-10-19), `requestDevice` filtered on the MIDI
   service `03B80E5A-EDE8-4B33-A751-6CE34EC4C700`, notifications of the MIDI I/O characteristic
   `7772E5DB-3868-4112-A1A9-F2669D106BF3` decoded by `BLEMIDI.parse` (the header and timestamp bytes, running
   status, real-time bytes anywhere, a SysEx across packets, the 13-bit clock's low bits wrapping; the rules are
   `../fm1-midi-bridge/docs/ble-midi.md`) and fed to `pinMsg` as a Web MIDI port's messages are, each stamped with
   the receiver's time less the packet's spacing (the clock follower keeps working). The device connected is listed
   first in **MIDI in from** and chosen ("Bluetooth: FM-1 MIDI"; another input chosen there makes the Bluetooth one
   wait), and the box says what it holds from the start instead of staying empty until Listen. A connection lost is
   retried three times. The peripheral is a Bluetooth MIDI pedal, or an FM-1 running the MIDI bridge (TRS in to BLE): the
   player on a phone with nothing else installed. Not in it: sending (the editor's SysEx stays on Web MIDI; BackBeat
   has no radio). Tests: test_web.mjs (the parser's cases, the page's wiring, both languages). To try by hand: a phone
   and a BLE MIDI source; the Linux desktop here has BlueZ without its MIDI plugin, which does not matter to the page
   (Chrome talks GATT itself) but means no local peripheral to test against.
6. **The screen (done 2026-10-11, first version).** On the TRACKS screen (the one on stage) the title's
   place shows the section playing (the song's name for it, or A..D) and under it the bar it is in,
   grey and "cued" while stopped; the parameter pages' top bar shows the same in the octave's place.
   The pedal's events are said in the top bar for about 1.5 s: "NEXT: CHORUS", "SONG: BLACK",
   "ENDING: OUTRO", "FILL NEXT BAR", "STOP", "NO SONG 3", "SECTIONS A-D". The UI reads only variables
   the sequencer keeps (`sng_ui_name`, `pedal_ui`), so SLOOP's UI tests, which stub the drummer, still
   build. While a request is pending the line under the section shows the one coming, ">SOLO", in
   amber (or ">STOP" for an ending with no outro). Not done: the pedal channel in the menu (no free
   row or tab; the tool sets it), the song's name on screen while playing.
6b. **The song screen (done 2026-10-13, `ui_drummer.c`).** SLOOP's SONG page (SAVE tapped on TRACKS) shows it
   while a song is loaded, or while none is and KNOB 1 + REC did not pick SLOOP's chain (opening the page brings
   it back). The song, its sections (the one playing lit, the one cued underlined, a meter other than 4/4), the
   pedal's messages. The keys reach it as the DRUMS grid page's do (seq.c `kb_grid` 2: the keyboard plays nothing,
   the UI gets the keys) and call `pedal_cc` with the IRQ off, the function the MIDI queue calls: white keys
   1..8 CC 20, 9..16 CC 27 (since 6d: 1..16 CC 20, OCT+ held CC 27), the black keys F#3 CC 25, G#3 CC 24, A#3 CC 21, C#4 CC 22, D#4 CC 23, F#4 CC 26
   (since 2026-10-14 any black key CC 21; OCT+ held, CC 21 value 1: the whole next bar, 2026-10-16);
   PLAY is CC 20 of the section cued (the count-in too) or stop. With no song the white keys 1..8 load songs.
   The section playing's key is lit, the cued one blinks. Small for the flash: no graphics, no bar counts; the
   pedal channel and count-in settings are still the tool's and the editor's. Tests: ui_pages_test.c (a stand-in
   song: the page, SLOOP's chain and back, the keys as the pedal, the LEDs, screens `page-song-drummer*.ppm`).
   On the emulator: `tools/fm1_emu.py` (fm1-emu has no USB, so the songs are written into the flash image of a
   copy of the package as storage.c writes them, and the unchanged firmware loads them at boot; the copy is
   `build/emu/felucca-emu.fwsc`, for the emulator only). Tried there 2026-10-13: the page, a song loaded by key,
   PLAY, the pedal's CC 20 over the TRS jack cueing the next section, the stop, a 7/8 song's meter.
6c. **A song-centred device UI (done 2026-10-14, five stages; README "On the device").** Asked 2026-10-14 ("help
   me redesign the UI so that it fits this use case better"): the device screen and buttons, songs and light
   editing, section edits saved back into the song, SLOOP's A..D kept out of the way, TRACKS, the visualiser, the
   GLO pages and layer and the HOME-held menu gone, the settings on the device. Four screens, a button each:
   - **LIVE** (HOME, the boot screen; `ui_drummer.c`): the song and slot, the tempo, beat lights in the bar's
     meter, the section large with "BAR n / m" and a bar-ticked progress (the count-in's beats while it clicks),
     what comes next in colour, the 8 sections as tiles laid out as the white keys, a footer with the kit, the
     pedal channel and a MIDI dot (`pedal_seen_ms`, set in `pedal_msg`). SEL / ENV / LFO are FILL / PREV / NEXT
     (CC 21 / 25 / 24); KNOB 1..4 tempo, kit, drums level, swing. ISR-written strings and the clock are read
     with the IRQ off.
     The keys (asked 2026-10-14, "I don't want to force a fill on every transition"): the white keys change section
     with no fill (`pedal_section` PS_END: when the section playing ends; 9..16 PS_BAR: on the next bar), every
     black key is a fill (CC 21; to the pattern's end then, the rest of the bar since 2026-10-15; with OCT+ held the
     whole next bar, CC 21 value 1, since 2026-10-16), so a fill into a change is a black key then a white one;
     ENV / LFO step without a fill. (2026-10-19, asked: "make the first black key do a fill now, the second a
     fill-next-bar, and the fourth a break": F#3 the fill as before, G#3 CC 21 value 1 without the hold, C#4 the
     break, CC 28 value 127: the section playing through its break when it ends; A#3 and the rest as F#3.
     `key_black` in ui_drummer.c. Test: ui_pages_test.) The pedal followed the same day: no section change brings a fill (see the
     command map); the web editor's player (`PLAY`) mirrors it. Found while doing it: the wait for the section's
     end (`pedal_at_end`) itself made the last bar a fill, so LIVE's first fill-free keys still filled into a
     section longer than a bar; `live_block` no longer does.
   - **SONGS** (SAVE tapped): the 8 slots from a cache (`sng_lib`, refreshed after a load, an editor write or a
     section save: `sng_slot_name` read a whole copy for its CRC every frame) and JAM, SLOOP's A..D chain page.
   - **EDIT** (SEQ tapped: the drum grid): SAVE held 0.7 s + 1 s writes the section playing back into its song
     (`sng_pack_section`, the reverse of `sng_apply`, staged in `proj_tmp`, then `st_save` and the copy indexed
     again); stopped only; nudges and locks are not kept (no room in SNG2). Applying a section now clears the
     undo, and a cleared hit takes its fill mark with it (rotate and x2 move the marks).
   - **SETUP** (GLO tapped; `ui_menu.c` reworked): 12 sections of up to four rows, globals bound to `song.g`;
     PEDAL (channel, count-in, load the last song at start: `arrset_t` spare bytes `autoload` and `last`, saved
     by `arrset_save` from `sections_flush` once stopped; `arrset_boot` asks for the song).
   LIVE's state line says FILL > CHORUS when a fill runs into a change (render `live-fill-next`); the web player
   shows the same. Checked with ui_pages_test (renders `live-*`, `page-songs`, `setup-*`, `hold-save-section`), song_test (every
   section of every song packs back to the same bytes) and on the emulator (a hit added, SAVE held, the song
   reloaded with the hit). The emulator shows the loaded image through XIP as loaded, so `tools/fm1_emu.py`
   leaves the song sectors out of the image when no songs are given. The emulator also showed SELECT doing
   nothing in SETUP: SLOOP's menu read SELECT, then dropped every knob at the end of the pass (`enc_drop`), and a
   step arriving in between (the input IRQ runs as a knob read turns interrupts back on) was lost; it now drops
   only ALGORITHM, the knob SETUP does not use (on the device it would have lost turns now and then).
6d. **16 sections a song (done 2026-10-14).** Asked 2026-10-14. `SNG_SECTIONS` 8 -> 16 (seq.c); the format does not
   change (SNG2's `nsec` is a byte), the limit is the packed song's 3800 B: sections of 16..48 steps take 56..150 B,
   so 16 fit (the test song, `tests/data/sixteen.json`, 1156 B); sixteen dense 64-step ones do not, and the packer
   says so. LIVE: white keys 1..16 are sections 1..16 (PS_END), **OCT+ held** + a key on the next bar (PS_BAR: the
   ISR samples OCT+ as the key goes down, `KB_OCTUP` in the lk_q entry; the state line says KEY: ON THE NEXT BAR
   while it is held; since 2026-10-16 the same hold with a black key or SEL is the fill on the next bar, CC 21
   value 1). The tiles four to a row, a section always in the same place, as many rows as the song
   needs: 1 row (≤ 4) the whole name on two lines, 2 rows (5..8) as before, 3 and 4 rows a line each (number, name
   cut). The pedal's CC 20 / 27 / 22 carry 0..15; `fm1_song.py`, `gp2song.py` (keeps 16, the outro last) and the
   editor follow; its player's keys 1..8 reach 9..16 after `0` (Alt + a digit switches browser tabs on Linux).
   Checked: ui_pages_test (renders `live-rows1..4`, `live-nextbar`; keys 10 and OCT+ + 16), song_test (a
   16-section song through PC, CC 20 15, CC 27 12, NEXT, a section saved back; 17 refused), the web tests, and
   on fm1-emu (`tests/emu_sixteen.sh`). To verify on hardware: OCT+ held with a key while playing, the 4-row tiles
   read from the floor.
7. **Hardware.** Everything above on a real FM-1: the TRS jack, the cable type, the UART driver, the
   speaker. Before the first install: `sh tests/run_tests.sh` green (but for the known `fm1_alnk0_irq`
   budget failure, identical on untouched SLOOP 2.6), then SLOOP's installer page with `build/felucca.fwsc`.
   **Installed 2026-10-19** on the first unit: tests green, a full dump of the stock flash first (the soft
   key into the ROM, jl-uboot-tool; nothing of the unit's where the songs go), then `tools/fm1_install.py
   build/felucca.fwsc` from stock V15 in one go; the FM-1 enumerates as "Felucca MIDI 1" plus USB audio
   and answers `FM-1_900`. A reinstall of the same package started the loader from BackBeat (step 1 works)
   but the loader dropped off USB a quarter of the way in; nothing was written (equal sectors are skipped)
   and BackBeat started again: cause open, see `../fm1-midi-bridge/docs/recovery.md`, "The first unit".
   The procedure is `../fm1-midi-bridge/tools/fm1_safe_install.sh`. **Played 2026-10-19 afternoon** (the user):
   the Ampero's MIDI OUT into the TRS jack over a plain stereo 3.5 mm cable (so Type A), Another Brick in the
   Wall and Back in Black start to end, cued from the footswitches with fills and section changes, heard on the
   speaker and from the audio out into the Ampero. So the TRS jack, the cable type, the UART driver and the
   speaker are facts. Not yet tried on the unit: every other "verify on
   hardware" note above. **2026-10-22** (the user, Ampero firmware 1.6.5): CC 24 / 25 (next / previous) work; the
   Ampero sends a footswitch's messages on its release, the same on every press (an effect slot's switch too), CCs
   only (a PC from a patch change, up to six messages), a channel per message; holding a switch sends nothing, a
   scene change sends no MIDI (`docs/ampero-setup.md`). The FM-1 (SETUP > PEDAL > MIDI CLOCK IN TRS) and the web player on
   a tablet (through a TRS-to-MIDI adapter) both followed the Ampero's MIDI clock; its START / STOP not tried yet.
   **TRS to USB** (asked 2026-10-22, to replace a DIY TRS-to-USB MIDI bridge for the tablet that mirrors the FM-1):
   SETUP > MIDI 2 (LEARN until then) > TRS TO USB, off by default (the settings word's bit 28). On, midi_uart.c also
   queues every channel message and realtime byte from the jack for USB (usb.c `midi_thru_q`: its own ring, filled
   and emptied in the TIMER5 ISR; not SysEx, which would interleave with the editor's on the one cable, nor system
   common); the editor lists the FM-1's own port in MIDI in from (a listener, so its link keeps the port). On core 0:
   a few hundred bytes a second at most, not worth the second core (asked). 304 B of code. Tests: midi_uart_test,
   ui_pages_test. **Verified on the unit 2026-10-22** (the user): the tablet's player following the Ampero through the
   FM-1, ON and BRIDGE ONLY. **BRIDGE ONLY**
   (asked the same day): a third value (bits 28 and 29), passed on and not for the FM-1 (midi_uart.c returns after the
   thru: no pedal, song, clock or note from the jack); LIVE's state line says BRIDGE: PEDAL TO USB. Measured on the
   tablet (wireless adb, DevTools): the FM-1 to the page 1.6 ms, the clock even (sd 1.5 ms), the player locked to it
   within about 1 ms as heard; what was left was the screen's delay (the editor's Screen ahead, 50 ms by default).
   Installed the same day, BackBeat over BackBeat (`fm1_safe_install.sh --no-dump`): written to 100 %, `FM-1_900`
   (the loader drop of 2026-10-19 not seen again: `../fm1-midi-bridge/docs/recovery.md`).
   **Tap tempo on the device** (asked 2026-10-22; GLO, then REC, considered first): the last black key, F#5, on the
   song screen taps the tempo (pedal.c pedal_tap, the pedal's CC 26), song loaded or not; its light flashes the tempo
   (ui_input.c tempo_led: PLAY's flash while playing, the count-in's clicks, free at G_BPM stopped). The other black
   keys keep their fills and the break. Tests: ui_pages_test. To verify on hardware: the taps and the light.

The fork is **rebased** onto SLOOP upstream as it moves (2026-10-12: its editor and installer as an app
that works without internet; the offline copy keeps the importer and the kit too), so `git log
upstream/main..HEAD` is exactly the fork's own work: `git fetch upstream && git rebase upstream/main`.
`rerere` is on in the repository (`git config rerere.enabled`), so a conflict resolved once is replayed
on any later commit that hits the same hunk. In practice a conflict comes once per file region (the
commit that first rewrote it), not once per commit: the README, which the fork replaced whole, is taken
as the fork's (`git checkout --theirs README.md`); `web/editor.html` has not conflicted so far, since
upstream's additions sit at its head and tail and the fork's in the middle. The fork's own files are
listed in the README. Since the drums-only work (2026-10-14) the fork no longer follows upstream: SLOOP's synth
side, most of its editor and its project format are gone here, so a rebase would conflict everywhere. A fix
upstream to what is shared (the drum track, the sequencer, storage, USB) is ported by hand, cherry-picked
where it applies.

## Flash budget

Audit of the 2.6 + M3 image (nm, 2026-10-11): `.text` 569800 B, of which the sampled instrument sets and
the acoustic kit (`SMP_DATA`) 303054 B, the UI with font and icons about 70 KB, the punch FX code about
10 KB (its 64 KB ring is RAM), the sequencer, project and storage code about 45 KB, the editor 19 KB, USB
MIDI and OTA 17 KB, and all twelve synth engines together under 30 KB. Dropping engines gains little;
the only big lever is `SMP_DATA`, and the acoustic kit is in it.

| Item | Size |
| --- | --- |
| Flash, total | 1 MB |
| App area (0x04000..0x93000) | about 572 KB |
| App slot (`tools/fm1pkg_make.py` APP_SLOT, `firmware/app.ld` XIP) | 581564 B |
| SLOOP 2.6 image, and this fork after milestone 3, after the fills by mark and the two layers, with the count-in, with SNG2 and the meter, with the song screen | 573052 B, 575660 B, 578532 B, 579440 B, 579876 B, 581448 B: 116 B left |
| Drums only (2026-10-14, `docs/drums-only.md`): the sampled instrument sets, punch FX, SLICER, live recording, the synth parts, engines, presets and FM6, three of the four tracks | 248780 B: **332784 B left** |
| Static RAM (.data + .bss) / the pool, drums only | 64672 of 98304 B / 200124 of 344064 B |
| After the UI redesign (2026-10-14, 6c: LIVE, SONGS, SETUP, the section save; the visualiser out) | 244556 B: **337008 B left**; static RAM 57664 B, the pool 194620 B |
| With the layered kit MULDJORD (2026-10-14: 289642 B of ADPCM, set REAL; 12 drum voices) | 541192 B: **40372 B left**; static RAM 59408 B |
| The five GM-sampled kits (ACOUSTIC, DEEP, TIGHT, BRIGHT, DUST: the CC0 kit as set PERC, 86144 B of ADPCM) gone, 2026-10-15 evening: nobody played them since the grooves went on MULDJORD; every kit number 5 lower (808 is 0, MULDJORD 41) | 471360 B: **110204 B left**, spent on MULDJORD's crash (below) |
| MULDJORD's crash (two takes, 3.2 s) and the snare, open hat and ride at their recipe lengths, 2026-10-15 evening: 385288 B of ADPCM (`tools/realkit.json` budget 412000); round-robin hats from the Muldjord hi-hat tried and reverted (the user prefers Black Pearl's) | 549360 B: **32204 B left** |
| With entries and breaks (CC 28, 2026-10-18; the flams and the fills to the section's end before it) | 550984 B: **30580 B left**; static RAM 61312 B |
| The ride 2.0 s (399478 B of ADPCM), the voice pick (at most four of a note, the quietest given way) and SETUP > LOAD, 2026-10-22 | 566616 B: **14948 B left**; static RAM 61344 B |
| Data region (0x93000..0xE0000): projects, the user sample slots of 80 KB (USR1 and USR2 now hold the songs: USR3 and USR4 are the user's; free: 0xC4000..0xC7FFF, the old user preset banks 0xDC000..0xDFFFF, the FM6 bank 0xE5000..0xE6FFF, 0xFB000) | about 308 KB |
| Songs 1..8 (0xA0000..0xAFFFF, A/B) and the pedal settings (0xB0000 / 0xB1000); songs 9..16 (0xB4000..0xC3FFF, A/B, since 2026-10-14) | 72 KB of USR1's 80 KB, 64 KB of USR2's; the kit pair USR3+4 stays the user's |
| ADPCM sample audio | about 11 KB per second (mono 22 kHz, 4 bit) |

So a general-MIDI-grade kit with many velocity layers does not fit. A drums-only firmware that
reclaims the synth engines, effects and sampled instrument sets, plus the data region, has roughly
400 to 500 KB for drum audio: about 40 seconds. Enough for one good acoustic kit with two layers.
Milestone 4 took the cheap way (no code stripping; the kit on the user slots, with layers). The image
fills the app area (milestones 1 to 3 cost about 2.6 KB together, the marks and layers about 3 KB more, SNG2
and the meter 0.4 KB, the song screen 1.6 KB, trimmed to fit), and static RAM is at about 92.8 of 98.3 KB:
songs are therefore read from flash in place. The flash was then full (the earlier "7 KB of headroom" was
measured against a wrong slot size: the slot is 0x8DFBC).

**Drums only (2026-10-14).** SLOOP's synth side is out in ten stages (`docs/drums-only.md`): the image went from
581448 to 248780 B, static RAM from about 92.8 to 64.7 KB. The room is for the drums: a more realistic kit in the
app's own flash (future work below), the song screen's settings rows, bar counts (both done, 6c), more songs.

## Fills, mark by mark (2026-10-11, evening)

The user expected to mark each box of the grid as a fill, and that is the better model: SLOOP's fill condition
covers a whole step, every lane at once, which forced the "additive" fills and the dropped hits. The drummer
now marks a section's hits one by one: a lane at a step is "only in a fill" or "not in a fill". In the
firmware two 16-bit masks a step for the drum track (`seq.c drum_fonly / drum_fno`, 256 bytes), taken out of
the lanes a step plays (`drum_step`, the ratchets too), set by a song's section (format flag 2: four bytes a
step after the drum steps), cleared with the pattern and by a project load; SLOOP's whole-step condition
still applies on top. In the JSON `"fills": {"HITOM": "............F..."}` per lane (`"fill"` per step stays
for SLOOP's). The importer marks the fill bar's own hits F and the pattern's hits it leaves out N, lane by
lane: nothing dropped any more. The editor's pen has a fill mark (always, only in a fill, not in a fill);
with it set, clicking a hit that is there changes its mark; cells show an amber or red ring. The player and
the packers (Python, JavaScript) follow. Found on the way: the review's project-load fixes (the song unloaded
by a project, USR1 not scanned at boot) had never landed because their anchor text differed; applied now.
Since 2026-10-14 a section change brings no fill of its own, so the imported fill bar's F hits play when the
player asks for the fill (CC 21, with the change or alone; LIVE's black keys or SEL). Since 2026-10-15 the importer
writes a fill bar of its own instead (below), and the marks are for hand-made sections without one.

## The GM kits out, the flash to the cymbals (2026-10-15, evening)

Asked "if I forego user kits, will the built-in kit be much better?": no, 160 KB buys fullness, not a different
kit. The user chose instead to drop the five GM-sampled kits (ACOUSTIC and its treatments, 86 KB of ADPCM plus
their code, unplayed since the grooves went on MULDJORD) and spend the room on the hats and the crash, keeping the
user kit pair USR3+4. Done: drums.c without the PERC set and the treatments' pitch shifts (`DRUM_DS0` is kit 0,
the first synthesised one; `drums.set` gone), `tools/gen_samples.py` without `gm_kit`, the golden list without
`drums/gm_kit` and the five grooves (the other 44 renders' hashes unchanged), the CPU baseline rewritten (its
reference groove, kit 0, is now the 808: the per-mille numbers moved with it), the editor's and the packers' kit
lists and defaults, the grooves and the example on kit 41. A song from before with `"kit": 46` plays MULDJORD still
(the device clamps the number; the editor shows the number); `"kit": 0` would now be the 808. Then, the same evening,
the user asked for MULDJORD as kit 0: it leads the list (`DRUM_REAL` 0, the synthesised kits from 1, `DRUM_DS0`), so
a song's default is 0 and the knob starts on it; the grooves, the example and the user's songs (songs/, backed up to
`../fm1-drummer-songs-backup-2026-10-15/`) say 0; the golden and CPU lists renumbered (no render changed).

The flash went to the crash (two takes, 3.2 s), and the snare, open hat and ride got the recipe's full lengths back.
Round-robin hats were tried the same evening: Black Pearl's hats have one hit a layer (no round robins), so the closed
and open hats were taken from the Muldjord kit's own hi-hat (4 x 3 and 2 x 2 takes), which cost the crash its ring
again (2.6 s) and the closed hat its length (0.24 s); renders of CLASSIC ROCK and FOUR FLOOR before and after were
made, and the user preferred Black Pearl's sound: reverted (commit eb714cf and its revert). Hats with round robins
would need another hi-hat or the user kit pair. The recipe keeps the older lanes' lengths as floors so a fit only
shortens new material; `seq2_test` seeds the round robins so two runs match.

## Fills as the arrangers do them (2026-10-15)

The user found the fills "still a bit counterintuitive" and asked for a comparison with arranger keyboards. Yamaha
(PSR, Genos), Korg (Pa), Roland (BK, E-A), Ketron (SD), Casio, the Alesis SR-16 and the BeatBuddy pedal share one
model, and BackBeat's (fills aligned to the pattern's cycle: to its end, or the next cycle when asked in its last
bar) departed from it in two places, so it was replaced by the common model the same day (firmware, the browser
player, the docs):

- **A fill is one bar, from the press to the bar line**, the phrase cut from the front (pressed on beat 3, its beats 3
  and 4 are heard: Yamaha, Korg, the BeatBuddy all do this). On the device: `pedal_fill` from the press, over at the
  next bar line (`pedal_fill_bars`, counted down in `live_block`); the fill bar of its own plays at the bar's own
  step (`drum_src`: step i of the bar is fill step i), a section without one by its marks and conditions in that
  bar. A fill bar of more than one bar (`fb_bars`, `fb_bar` steps each, from `sng_apply`) ends that many bars on,
  the phrase ending with it.
- **Pressed late, the next bar**: in the bar's last beat (Yamaha's rule, Casio's; the BeatBuddy's is the last half
  beat) the whole next bar is the fill (`pedal_fill_wait`; the LIVE screen says FILL NEXT BAR). One rule, no
  cycle arithmetic; `tests/song_render.c --fills` asks for its fills this way, just before the last bar.
- **After the fill the pattern restarts from its top** (an arranger's variation after its fill-in), its bar count
  with it (`seq_reset_tracks`, `live_bar = 0`), unless a section is cued: then that one lands. The quick chain
  keeps its count. Since 2026-10-17 only after a fill bar of its own (below).
- **A fill pulls any change to its end**: a section asked for with a fill (CC 21 with CC 20 or 27, either order,
  before or after) lands on the bar after the fill, not at the pattern's end (`pedal_at_end` is cleared by the fill
  request, not set by a request made during one). An ending's outro the same; an ending with no outro stops at the
  fill's end.
- A fill asked for again while one plays changes nothing (it ends at its bar line), except in the last beat of its
  last bar: then the next bar is a fill too, straight on, the pattern restarting after both (asked 2026-10-16: the
  press was ignored there; `pedal_fill_wait` while `pedal_fill`, chained in `live_block`). The "fill hits ahead"
  rule and the next-cycle rule are gone with `pedal_fill_hold` and `pedal_fill_hits`.
- **A section without a fill bar of its own: fill mode to the section's end** (asked 2026-10-17: "play the remaining
  of the section in fill mode"; "if there's no fill whatsoever, fill should not reset the section"). The one-bar
  model above fits a fill bar, whose phrase is cut from the front wherever the press lands. A fill written into the
  section's marks (F / N per lane, or SLOOP's step conditions) is positional: a 4-bar verse with its fill in bar 4
  never played it unless the press was in bar 4; pressed in bar 2 the player heard bar 2's marks, usually none, and
  the pattern cut back to its top. A section with no fill at all had its phrase chopped by the restart for nothing.
  So without a fill bar: `pedal_fill_bars` is the bars to the section's end, this one counted (`fill_bars_from`
  in seq.c, used by `pedal_fill_req` and `live_block`'s two waits), `fill_now` stays on through them, and at the
  end there is no restart (the section is at its top; `live_bar` counts on, so a request waiting for the end and
  the quick chain stay right). One rule for both kinds without a "has marks" test: a section with nothing marked
  shows FILL on the screen and plays on unchanged. Pressed in the last beat of the section's last bar (or value 1
  there): the whole next cycle is fill mode (the alternative was a fill of zero bars); the chained press in the
  fill's last beat the same, another cycle. A change cued with the fill lands at the section's end, the same place a
  plain change would; an ending's stop too. A 1-bar section without a fill bar: as before minus the restart. The
  browser player (`PLAY.fillBars`) mirrors it; `tests/pedal_test.c` (SLOOP's A..D have no fill bar) and
  `web/test_web.mjs` assert the count going on; the fill-bar cases in `tests/song_test.c` are unchanged.
- Not done, cheap now that fills are bar-aligned if ever wanted: hold-to-loop (Korg, the BeatBuddy) for a footswitch
  that sends its release. The break (Yamaha's, Korg's, Ketron's, the BeatBuddy's pause) is CC 28 since 2026-10-18
  (below: entries and breaks).

## Entries and breaks (2026-10-18)

Asked 2026-10-18, after a day of playing with a footswitch a section (scenes 1..5, start and stop on 6 and 7): "for
songs that have a silent bar or two requiring a separate section for the interval is not good". A section for the
silence costs a switch and a second press on the bar. Shine On You Crazy Diamond's drum track (the GP4, bars 166-170
and 204-208) showed the shape: P3-SYNTH's 3-bar phrase ends with its fill at 165 and the ride groove's starts at 170
(its fills sit on 181, 187, 193, 199, 202: the phrase's last bar), so the way between them is **4 bars where the cycle
counts 3** (the user, the same day: "there's an extra bar between the 3-bar pattern, specifically I think the extra
bar is 168"); 204-207 is a fill bar and a 3-bar break. So:

- **An entry and a break, a section's own**, each bars written (any lanes, silence too, whole bars of its meter, 64
  steps at most) played **before its pattern** as it comes in; after them the pattern starts from its top
  (`live_block` restarts it, as after a fill bar), and a section asked for meanwhile lands at their end: while one
  plays the section's bars are its bars (`eb_bars`, `live_sec_bars`). So a 4-bar break on P3-SYNTH with P3-GUITAR
  pressed during it puts the groove's top on 170, as a 4-bar entry on P3-GUITAR does. (The first form, the same
  morning, laid them over the section's first bars with its count running underneath, the pattern then going on at
  bar N mod its length; read from the transcription as "the cycle keeps counting", it put the groove a bar off, and a
  change pressed during a 4-bar break landed after 3.) The steps are the drum track's 0..eb_len-1 from the landing
  (`drum_src`, by the absolute step).
- **The entry** plays when the section comes in from another one or from stopped (after the count-in), whatever
  brought it (CC 20, 22, 24, 25, 27, LIVE's keys). Never when the section playing is asked for again: that stays a
  no-op, which a switch sending a fill with its own section relies on (Another Brick: the crash fills in the chorus).
- **The break** is a command of its own, CC 28 (the user's choice over "press the section playing again"). CC 28 n
  waits as CC 20 does and composes with CC 21 the same way: the fill (its fill bar or its marks), then the break. CC 28
  with a value that is not a section is the section playing's break, so one BREAK switch serves every song; stopped,
  the section cued (else the first) starts with it. A section with no break falls back to its entry, then to a bar of
  rest, which also serves SLOOP's A..D.
- **What cuts one**: a fill (CC 21) pressed during it (the fill takes over as from the pattern; `eb_len = 0` where the
  fill starts), a stop, another landing. **A fill right after one** (asked 2026-10-18): CC 21 value 1 in its last bar,
  or a plain CC 21 in its last beat: it plays out, the next bar is the fill from the pattern's top (the fill bar, or
  fill mode for the whole cycle: `fill_bars_from(0)`, not the break's bar count), then the pattern. CC 28 itself
  waits for the section's end, as CC 20; a "break on the next bar" (as CC 27) is not done. External MIDI START with nothing cued: no entry (no landing).
- **The device**: `sng_eb` decodes the entry or the break at the landing (`live_block`, `live_req_brk` from
  `pedal_section_to`) into one buffer (`eb_dstep`, `eb_flam`: 768 B of RAM; only one plays at a time), read as a fill
  bar is (no condition, no mark, its flams). The section save keeps both as they are. The LIVE footer says
  "BREAK: name" while one waits, the top bar "BREAK: name" on the press.
- **The format**: flags bits 2 and 3 (`SNG_F_ENTRY`, `SNG_F_BREAK`), each a byte of steps and the steps, after the fill
  bar, in that order (check code 7 for a bad block). Old firmware refuses such a song (code 6); the firmware and the
  editor ship together. JSON: `"entry": {"bars": 4, "lanes": {...}}`, `"break": {...}` (`tools/fm1_song.py`; "steps"
  instead of "bars" allowed, whole bars); `tools/fm1_render.py --play "... !GROOVE:6"`: that entry through its break.
- Tests: `tests/song_test.c` (checked, saved back, from stopped and from another section, never on a re-press, the
  pattern from its top after a 4-bar entry into a 3-bar section and a change pressed during it at its end, not at the
  pattern's 3 bars, CC 28 on the section playing with the entry as
  fallback, a bar of rest, with a fill on one switch, from stopped after a count-in, a fill cutting one, SLOOP's A..D),
  `tests/song_bars_test.py` (the JSON side). Rendered: the user's Shine On (P3-SYNTH's 4-bar break, bars 166-169,
  P3-GUITAR pressed during it) sounds as the GP4, the ride groove from its top at 170. To verify on hardware with the
  rest.
- **The browser player** (stage 2): `SNG` packs, checks and unpacks both (in bars when whole: the same bytes as
  `tools/fm1_song.py`), `PLAY` lands them as `live_block` does (`PLAY.ebOf`, `st.eb` over `st.step` from the landing;
  `PLAY.brk` is CC 28), the transport's **Break** (CC 28 127, key b), the pedal input's CC 28, "BREAK: name" on the LIVE
  line. `tests/example-song.json` carries an entry (CHORUS) and a break (SOLO). Tests: test_web.mjs (editorEntryBreak).
- **The section editor** (stage 3): **Entry** and **Break** selects beside the fill bar's button (none, or 1 to 4 bars of
  1/16 in 4/4, 64 steps at most), a new one silent; their columns after the fill bar's, each after a wide blue gap,
  blue dashed cells on the pen (level, ratchet, flam; no marks), bar lines inside; **Entry = selected bars** and
  **Break = selected bars** in the file's bar strip (1 to 4 bars, as they are: Shine On's 166-169 and 205-207 in one
  click each). Through split both go with the first part, through merge the first's (`SED.ebSet`, `ebSetHit`,
  `withBlock`, `norm` in bars). Tests: test_web.mjs (editorEntryBreak's sed part); looked at in headless Chrome.
  Copy and paste reach the blocks (asked 2026-10-18: "copy sequencer entries into fills, entries, and breaks"): the
  fill bar's, entry's and break's step numbers select steps as the pattern's do (`SONGED.colKey`), so Copy / Paste /
  Clear (Ctrl+C / Ctrl+V / Delete) work there; pattern steps pasted into a block keep their levels, ratchets and
  flams, not their marks or conditions (a block plays as written); a block's steps paste back into the pattern
  (`SED.copyCols`, `pasteCols`, `clearCols` with a block's key; `SED.view`).
- The user docs (stage 4): README (the pedal table, entries and breaks), `docs/ampero-setup.md` (a switch a section,
  the BREAK switch), `web/EDITOR_PROTOCOL.md` (the song objects' new blocks).

Host-tested (`tests/pedal_test.c`, `tests/song_test.c`, `tests/ui_pages_test.c`, `web/test_web.mjs`); to verify on
hardware with the rest.

**Fills of several bars (asked 2026-10-20: "fill bars similar to entry and breaks, that can go up to 4 bars").** The
firmware, the format and the browser player already played a fill bar of more than one bar (`fb_bars`, since
2026-10-15: its first bar from the press, cut from the front, then its others whole, the phrase ending on a bar line,
then the pattern from its top or the section asked for); only the editor and the importer made one bar. Now:
- **The section editor**: the fill bar's button became a **Fill** select beside Entry and Break (none, 1 to 4 bars,
  64 steps at most, `SED.fbSet`, `fbBars`): a new one as before from the pattern's last bars the way a fill played
  them (`fbAdd(sec, empty, bars)`, rests before the pattern's start), shorter cut, longer rests; **Fill = selected
  bars** takes 1 to 4 bars of the file (`withFill` through `fromBars`).
- **The importer** (`gp2song.py make_section`): the bars that deviate at a section's end are its fill, up to 4 (the
  pattern's limit, 64 steps), only in a section that keeps to its pattern (`MATCH`) with a whole pattern before them;
  else one bar as before. The user's files: Back In Black's chorus and solo, Sweet Child's solo, Eye of the Tiger's
  last part got 2-bar fills.
- **It ends on the section's last bar line** (asked the same day: "make the long fill end on the section's last bar
  line"; before, it ran its bars from the press, past the section's end). A fill bar of several bars asked for with
  more bars left than it has waits (`pedal_fill_wait` is now the bar lines to its start: 1 the last beat's next bar
  as before, more a long fill waiting; `fill_wait_from`), then plays whole; with fewer left, its last bars from now,
  the first cut from the front (`fill_bars_from`); a pattern shorter than the fill (a 1-bar groove, a 2-bar fill):
  the first section end it reaches whole (`fill_span_from`). The last beat and CC 21 value 1 count from the next
  bar. A 1-bar fill is unchanged (the rest of this bar), and so is a section without a fill bar. The screen says
  FILL AT THE END while one waits (FILL NEXT BAR for one bar). A chained press in a fill's last beat still plays the
  next one straight on. The browser player mirrors it (`PLAY.fillSpan`, `fillWaitFrom`, `st.fillWait` a count).
Tests: test_web.mjs (the select, fbSet, a 2-bar withFill; the player: a 2-bar fill asked in a 2-bar section's last
bar and first, a 3-bar one waiting a bar, a 2-bar one in a 4-bar section waiting two), song_test.c (the device, the
same first three), gp2song_test.py. To verify on hardware.

## The backup holds the songs; the editor's Projects tab gone (2026-10-21)

Asked: "I don't see a purpose for projects in this tool". The project slots are SLOOP's (the device's JAM
PROJECTS: the Jam's sections A..D), so their card moved onto the Jam page, under the side bar's "future: not
supported yet" with the Sequencer; the Backup card moved to Settings, and the Projects tab went. The backup itself
missed what matters most: `ED_BK_IDS` (editor.c) listed SLOOP's objects only, the songs and the pedal settings were
readable and writable through `BK_GET` / `BK_PUT` (fm1_song.py, the Setlist tab) but not listed, so "everything on
the FM-1" left them out. Now `BK_LIST` lists 16..23, 40..47 (an empty slot with length 0: a restore erases it) and
24, after the samples; the editor writes them back before the working project and the settings (`BK.RESTORE`) and
reads the slots again after. A backup from before has none and leaves the songs alone. Tests: test_web.mjs (the ids
against editor.c, a backup with two songs and the pedal set, a restore that brings one back and erases another, an
old backup that leaves them). On the unit (2026-10-21, FM-1_900 installed over the bridge): `BK_LIST` lists 28
objects, the six songs there (slots 2..7) and the pedal settings among them, kept across the install, and every
object read back by `BK_GET` matches its CRC; the editor's Save backup and Restore on the unit work (the user, the same day). (Found on the way: tools/fm1_song.py
named the song header's size `HDR` too, over the SysEx header, so its link to the FM-1 had failed since SNG2,
2026-10-13; renamed `SNG_HDR`.)

## Review notes (2026-10-11)

An independent review of the firmware diff (interrupt safety, flash, bounds, logic) found no memory
hazard; what it found and what was done:

- A section request pending when another song lands, or naming a section the song does not have
  (SLOOP's keys 1..4, a quick chain), is dropped at the bar instead of resetting the tracks to nothing.
- SLOOP's SONG REC does not record a song's sections (they are not A..D); a project loaded over a song
  (SAVE + key while stopped, the menu, a restore) unloads the song.
- The pedal takes only its own messages from its channel (CC 20..27 and PC); sustain, learned CCs and
  the rest pass to the tracks as usual.
- USR1 is never scanned as a sample slot and the editor's backup lists it empty: its flash holds the
  songs. A device that had a USR1 sample from SLOOP shows it as empty here.
- The UI copies the names the ISR writes with the interrupt off; the song request is read and cleared
  with the interrupt off; the ending's stop ends a take as STOP does; a song load during a free take
  drops the take as a project load does.
- Known and accepted: a flash write started while the pedal starts the transport in the same instant
  drops the audio for about 50 ms (SLOOP has the same window with a MIDI START).

## Future work (the user's wishes, 2026-10-11)

- (done 2026-10-13, 5c) **The real pedal into the editor** (asked 2026-10-12; first in line since 2026-10-13, the simulator
  card dropped: the Play card's buttons already do every action). The editor opens only the FM-1's port;
  Web MIDI already lists every input, so a "MIDI in from" selector on the Songs tab and a handler feeding
  each CC / PC into the player as the Play card's buttons do (CC 20 / 27 -> `PLAY.request`, 21 ->
  `fillNext`, 22 -> `ending`, 23 -> `stop`, 24 / 25 -> `step`, PC n -> the slot's Play; the channel
  checked against the Pedal card's; a log line in the FM-1's own words) let the Ampero drive the browser
  player: the live workflow rehearsed with the real footswitches before the hardware. A small pure router
  next to `PLAY`, unit-tested; the mock has no second port, so the port side is tested by hand. Messages
  arrive outside a user gesture, so Play is pressed once first for the audio context. To check first:
  whether the Ampero II Stage sends MIDI over USB or only from its TRS MIDI OUT (then a USB MIDI
  interface in between). About half a day.
- (done 2026-10-11) **A section editor in the web editor**: see 5b.
- (done 2026-10-11) A kit of real recordings: the Muldjord Kit, chosen by ear (CC BY 4.0, credited in
  `LICENSING.md` and `web/kit/ATTRIBUTION.txt`).
- (done 2026-10-13, 6b; bar counts and the settings rows done 2026-10-14, 6c) **A song screen on the device** (asked 2026-10-13): a page of its own for the drummer, reached from
  the TRACKS screen (a key combination or the HOME menu), with the songs in the slots to pick (KNOB 1, the
  names from `sng_name`; loading as PC does: stopped at once, playing with the next section) and playback
  on the keys as the pedal would do it: the **white keys** are the song's sections (key 1 = section 0,
  the names drawn above them, the one playing lit, the one cued framed; a key again while it plays: the
  fill), the **black keys** the actions (play / stop, pause, fill, next section, previous section, the
  ending / outro, maybe tap). Everything goes through `pedal_section`, `pedal_fill_req`, `pedal_step`,
  `pedal_ending` and the transport requests, so the keys and the footswitches are one path and the host
  tests cover both. The screen shows the song, the section and its bar, the count-in, and the pedal's
  messages as the top bar does today. Also the place for the pedal channel and the count-in setting
  (the item below). Flash: about 6 KB left, so the page must be small (ui_draw.c's text and key
  handling only, no new graphics); check the budget first. Try it on the emulator (fm1-emu, which
  runs the package and feeds MIDI into the jack) before the hardware.
- (done 2026-10-14, 6c: SETUP > PEDAL) The pedal channel on the device's menu, if a place for it turns up (or on the song screen above).
- The kit's balance by ear (per-lane gains are peak-based guesses), and the Salamander ride as the alternative.
- The importer: tempo changes inside a song, pickup bars, a meter that changes inside a section (a meter per
  section is done: 3b).
- (done 2026-10-12) Undo and keyboard shortcuts in the section editor.
- (done 2026-10-13, 3b) **A compact packed song ("SNG2")**, asked for 2026-10-12 when a real song packed to 4336 bytes against
  the 3800 limit. The limit is a flash sector per song copy (4096 bytes, 256 of them the storage
  header), and the packed form is SLOOP's RAM drum step copied as is: 10 bytes a step (lanes on, four
  levels, four ratchets) plus 4 a step for the marks, so a 64-step section with marks costs 928 bytes
  and four fill the sector. Proposed: 2 bytes of lanes-on a step, then one byte per hit (level,
  ratchet, mark), which is 4..6 bytes a step for two to four hits, about 2.5 times the room, with the
  old form still read so songs in flash keep loading. Touches sng.c (check / apply unpack into
  dstep_t), the editor's pack / unpack, fm1_song.py, gp2song.py's size estimate and their tests.
  The alternatives, two sectors a song (four slots, and SLOOP's storage has no multi-sector objects)
  or authoring shorter sections, are worse.
- (done 2026-10-13, 3b) **A time signature per section** (asked for 2026-10-12, for 7/8 and the like). A pattern already
  loops on its own length, and the importer slices by each measure's tick length, so a 7/8 groove as
  14 sixteenths plays; what is nailed to 4/4 is the bar the pedal lands on: seq.c counts bars as four
  quarter-note beats of the transport clock in about six places (where a section request lands, the
  armed fill bar, the ending's bar count, the click accents, the count-in's four clicks), and
  `sng_bars` / `section_bars` divide by four beats too. Proposed: a meter in the spare byte of the
  section header (numerator and denominator), bars counted from the moment the section was applied in
  that meter (every section starts on a bar and resets its tracks, so the anchor exists), the count-in
  clicking the target section's beats, the player and the section editor taking steps per bar from
  the meter, a `"time": "7/8"` field in the JSON that gp2song.py fills from the measure's signature,
  a 7/8 case in pedal_test and test_web. SLOOP's own A..D stay 4/4. One to two days with the tests.
  Changes of meter inside a section (a bar map) were considered and discarded 2026-10-12 as too much
  complexity: such a passage is written as sections of one meter each. Best done with the compact song
  format above, since both change the packed form and its three readers (sng.c, the editor,
  fm1_song.py), so songs need re-sending only once.
- (done 2026-10-14) **Drums first: SLOOP's synth side taken out**, in ten stages (`docs/drums-only.md`): 332 KB of
  flash and about 28 KB of static RAM free. Kept: the drum track (kits, steps with levels, ratchets, nudges,
  locks and fill conditions, rolls, erase, the mutes), sections A..D and their chain, the songs and the pedal,
  the INSERT and FILTER, the delay and reverb, the master (DUST, DUCK, the DJ filter, the limiter), TEXTURE, MIDI
  in / out / clock / learn, the visualiser, the web editor (protocol v11).
- (done 2026-10-14, but by ear and on hardware) **A more realistic kit** (asked 2026-10-14): KIT **MULDJORD** (46), the
  power-on kit and new songs' default, built into the app's flash (289642 B of IMA ADPCM, set REAL; the user's
  USR3+4 stays theirs). Decided with the user: built in; balanced (4 layers on kick, snare, hats; takes on kick and
  snare; 2 layers on toms, open and pedal hats, rim, ride; the crash and ride long); today's mix of sources. Five
  stages: (1) `tools/gen_realkit.py` renders `tools/realkit.json` once from the source kits (fm1-mvave/kits, sox)
  into `assets/realkit/` (a WAV a take, `kit.json`), each hit its mics summed, trimmed, cut, the layers levelled
  on the velocity curve x 1.5 (0 / -3 / -7.5 / -14.5 dB: the sources' own range, 25 dB on the snare, lost ghosts
  under a guitar) and fitted to the budget; `tools/gen_samples.py` encodes it (set REAL, `RK_LANE`: per lane its
  layers' first zone, takes and velocities), so the build needs no source kit. (2) drums.c: `rk_layer` the layer
  whose velocity is nearest the hit's (a step's 127 / 100 / 72 / 42), `rk_take` a take other than the lane's last
  (an LCG: deterministic renders), the voice's velocity raised to full at its layer's own (a MIDI velocity between
  two layers a little under); NDRUM 6 -> 12 (static RAM +1.6 KB). (3) userkit_test (the layers, the levels, 60
  snares never twice the same take, a crash under eleven hits), the golden groove `46_MULDJORD`, CPU: its groove
  1094 per mille (the other kits 760..1290), the busy pattern on it 2165, the busy one on 808 1785 -> 3132 (twelve
  voices sound where six did; the loops' bodies unchanged; device static check `drums_mix` within budget); on
  fm1-emu the real image boots on MULDJORD and plays a song. Its loudness is the user kit's (the same sources,
  two layers: RMS about -25 dB in a song), some 7 dB under the synthesised 808 (the power-on kit before). (4) The editor's player plays it from `realkit/` next to the editor with the same rules.
  (5) these notes, `docs/kits.md`. Black Pearl's hats have one take a layer (no round robin there). To do: listen
  (renders: `fm1_render.py song.json --kit 46`), adjust the recipe; on hardware the CPU with twelve voices.
  **2026-10-22** (asked: the ride cut short, "I think it might be the voices"): a hit takes, with four of its note
  sounding, the oldest of them; else a free voice; else the quietest (velocity x the part of its sample left; a
  synthesised voice counts half), no longer the oldest (`drums.c drum_voice`, userkit_test). The ride 1.5 -> 2.0 s
  (16 KB; at 1.5 s it rang 16 dB under the hit). The CPU on the unit: SETUP > LOAD (ui_menu.c) shows what audio.c
  already measured (the average, the worst half buffer since OCT+, the late ones, the voices' peak and the hits
  that found none free; the console's `status` too). To verify on hardware: the busiest song's worst block, and
  whether 16 voices would fit (NDRUM; about 270 B of RAM a voice).
- **The drummer without the device** (lower priority, asked 2026-10-13): the PC, phone or tablet as the
  drummer, driven by the pedal, when no FM-1 is at hand, with higher-fidelity samples than the device
  can hold (longer samples, more velocity layers, round robins). Two routes: (a) the web editor's player,
  which already plays the real song format with the kit through Web Audio and runs as the installed app
  on Android: a "no device" mode for the Songs tab (done 2026-10-14: the tab, renamed Song, works without
  an FM-1; only the pedal settings and Send need one; the device's slots moved to a Setlist tab, and SLOOP's
  Song tab became Jam), the real
  pedal in (the item at the top of this list), a hi-fi kit folder served next to the editor or chosen
  from disk (untrimmed WAVs; the device's trims, two layers and ADPCM do not apply in the browser),
  latency checked on each platform; (b) the firmware's own engine as a desktop program (see "Related
  programs"), faithful but bound to the device's sample limits. Route (a) is the natural one: the pieces
  exist and the phone is already the editor's home.
- (done 2026-10-19) **MIDI file import** (asked 2026-10-16; see the end of this item). The plan: a Standard MIDI File (.mid, type 0 or 1) into a song, as tools/gp2song.py
  does for Guitar Pro: the drums are channel 10 (and any track whose notes are all in the GM drum range), the same
  GM note table to the 16 lanes (`GM` in gp2song.py: kick 35 36, snare 38 40, side stick 37, clap 39, hats 42 44
  46, toms, crash, ride, the rest dropped), velocities to the four levels where Guitar Pro gave accents and ghosts
  (under ~40 GHOST, under ~70 SOFT, over ~110 HARD; to tune on a few files), notes quantised to the grid the bar
  needs (16ths, or 12ths when the off-grid share says triplets, as `offgrid_share` picks it today; two notes on a
  step a ratchet), the tempo from the first set-tempo event, the time signature per bar from the meta events (the
  song's per-section meter), the sections from the marker meta events (the markers' names, as the Guitar Pro
  markers) or, without them, from the drums alone (`auto_sections`, the repeating runs of 1..4 bars, a lone
  different bar before a repeat as the fill bar), duplicates folded as now. The shape: a reader of SMF chunks and
  variable-length deltas in plain Python (no library: `mido` is not in Pyodide's wheels by default, and the format
  is a page), the rest of gp2song.py shared, so one importer with two front ends, `gp2song.py file.mid` and the
  editor's From Guitar Pro card taking .mid too (the drag-and-drop and the file picker already dispatch by
  extension). Why: MIDI drum tracks are everywhere (drum machine exports, DAW projects, the groove packs the
  rhythm sites sell), where Guitar Pro files are the tab sites' only; and a DAW is the easier place to write a
  part by ear. Tests: gp2song_test.py with a type-0 and a type-1 file written by the test itself (a few bars, a
  marker, a triplet bar, a tempo change), the same sections, levels and fill bar as the Guitar Pro twin.
  Done 2026-10-19, as planned with these choices. gp2song.py now reads a file into bars of its own (`Bar`: length,
  time signature, notes as position, lane, level, flam) through one of two readers, `read_gp` (PyGuitarPro, imported
  only then) and `read_midi` (the SMF chunks and running status in a page of Python, no library), told apart by the
  first bytes (`MThd`), and everything after the reader is the one code: patterns, fills, duplicates, the cuts to fit
  the sector, `bars` and `ranges` for the editor. The drums: channel 10 on any track; `--track n` one track whatever
  its channels; without channel 10, a track named for drums (DRUM, PERC, KIT, BATT, SCHLAG), else one whose notes all
  sit in 35..81 with a kick or a snare among them; none: the error lists the tracks. The levels are the same velocity
  thresholds as for Guitar Pro notes without accent flags (under 50 ghost, under 80 soft, 110 and over hard: one
  table, `level_of_velocity`, to tune for both at once). Flams: two notes of one drum less than a 32nd apart, the
  first the grace, the hit's own velocity (Guitar Pro has the grace as a flag on the note; a DAW writes it as a
  note); a drag written as two 32nds stays a ratchet, as before. Humanised parts: a note within a 64th triplet
  (1/24 of a quarter) of a 32nd line is put on it; 8th and 16th triplets (160 and 320 ticks of 960) stay off the
  grid and count as Guitar Pro's off-grid hits do (towards 1/32, not a triplet grid: the importer still never picks
  8T or 16T, for either format; a later item). The bars follow the time signature meta events (a change takes the
  bar line it sits on, or the next), the markers (cue points when there are none) take the nearest bar line, the
  tempo is the first set-tempo event and later ones are counted and said to be ignored; the title is the first
  track's name unless it names the drums, else the file's. The editor's card is **From Guitar Pro, MIDI or JSON**:
  the picker accepts .mid / .midi, the page sniffs `MThd` and loads Pyodide and gp2song.py without the PyGuitarPro
  install for a MIDI file (`gpiLoad(gp)`), so a MIDI import costs the Pyodide download alone. Tests:
  `tests/midi2song_test.py` (its own SMF writer: the Guitar Pro test's synthetic song as a type-1 file, with the same
  assertions, its bars equal to the twin's; a type-0 file with a 7/8 section, humanised notes, a tempo change; the
  drums found by the track's name; `--track`; no drums; junk), run unconditionally by run_tests.sh, where the Guitar
  Pro test still needs PyGuitarPro; `web/test_web.mjs` for the card. Not verified in a browser yet: the Pyodide path
  with a .mid on the tablet (the laptop's Chrome ran it for Guitar Pro files before; the reader itself has no
  browser-side code).
- (asked and done 2026-10-17) **Flams.** A hit flagged as a flam plays a grace note first: the same drum as a ghost,
  about 25 ms before the hit (`seq.c FLAM_SMP`, 1100 samples: a drummer's flam, the same at any tempo; a quarter of
  the step at most, so 32nds at 240 BPM still read), the hit itself on the grid; with a ratchet, the grace before
  its first hit only. The format did not change: the packed hit byte's bit 6 was spare (level 0..1, ratchet 2..3,
  mark 4..5), so SNG2 stays SNG2 and the editor's check now refuses only bit 7. In the JSON, `"flams"` beside
  `"ratchets"`: a string a lane, `f` where the hit is a flam, the fill bar's too (`tools/fm1_song.py`). The device:
  the masks `drum_flam` / `fb_flam` beside the fill marks (moved, rotated and cleared with them; the packer writes
  them back, so a section edited on the device keeps its flams; the EDIT page does not show or set them). The grace
  is predicted: `seq_flam` runs each block before the step logic, finds the step due next (the nudge and humanize
  shift included) and plays its graces once it is within reach (`t->flam_abs` remembers which step's), through the
  same note-on as the ratchets, humanize's hash of its own. Not for the first step of a bar while a section change,
  a fill's start or end, an ending or the chain could change what that bar plays (the prediction would be the
  pattern as it is): that grace is skipped rather than played for the wrong bar. The browser player mirrors it
  (`PLAY.FLAM`, `flamAt`: a ghost of the same drum scheduled 25 ms before, never in the past); the section editor
  has a flam box in the pen row (key `l`; `f` stays the player's fill), the cell shows a bar on its left, flams go
  through split, merge, copy, paste, the fill bar's own and the bar taken from the pattern; the Guitar Pro importer
  reads the file's grace notes as flams (two 32nds stay a ratchet, a drag written out). Why a grace note and not a
  ratchet: a ratchet's hits are even and equal, and at 120 BPM two in a 16th are 62 ms apart; a flam is 20..40 ms
  and soft first. Tests: seq2 (the timing, the level, a ratchet's, the cap, the bar in doubt), the example song
  (a flam on the chorus's first snare, the packers' round trip, the renders: golden updated), gp2song_test (a
  grace note), test_web (the packers, the section editor, the player). To hear on the device: whether 25 ms reads
  as a flam on MULDJORD's snare through the limiter, or wants 20 or 30 (one constant).
- (first stage done 2026-10-16) **A responsive site, usable on a smartphone** (asked 2026-10-16). The editor is laid out for a desktop and a
  tablet (the transport, the tiles in rows of four, the section editor's grid; the Setlist's 16 rows fit a tablet):
  on a phone it needs a layout of its own, not a scaled one. What that means: the tabs and cards stacking in one
  column at phone width, the Play card's transport and the LIVE view first (the phone on the floor or the mic stand
  is the drummer's screen), buttons at a thumb's size, the section editor's grid scrolling sideways with the lane
  names fixed, the tables (the set, the slots) as rows that wrap, the kit page's pads in a grid that fits, the help
  texts folded. The PWA already installs on Android; the manifest's orientation and the wake lock want checking
  there. Test it in headless Chrome at 360 x 780 as the tablet layout was, and on the user's phone over adb as the
  tablet is served (`docs/`, the reverse rule). First stage done 2026-10-16, on the user's phone over adb (1080 x 2400
  at 440 dpi: 418 x 788 CSS px in Chrome, the page driven through its DevTools socket, `adb forward tcp:9223
  localabstract:chrome_devtools_remote`, screenshots by `screencap`): a phone layer at 600 px and under, on top of
  the 860 px top-bar layout that was there. Live mode fits one screen with every element on it, a set of 16
  sections included (the set's row small, the LIVE view's head wrapping with the tempo row under the name, the
  section name at 52 px, the tiles and the pedal's two rows at 66 px with whole words, the grooves card and the
  count-in / humanize row away until the show is over; the user's complaint was that live mode did not fit at
  all). The frame: the download link folded (it is for a computer), the install button wrapping, the title bar
  wrapping, the set's and the grooves' rows wrapping their buttons. The section editor: the cells at 26 px
  (`--cell`, the grid's columns from the same variable) and the lane names sticky while the grid scrolls sideways.
  Still open: the tables, the kit page's pads and the help texts folded; the manifest's orientation; landscape.
- **JAM on the device, fixed** (asked 2026-10-16: "the Jam feature that we lost with SLOOP"). JAM is what is left
  of SLOOP's own way of playing: its sections A..D with the quick chain and the song chain, reached from the SONGS
  screen's last row (`sng_view DV_JAM`, `drummer_load(SNG_JAM)`: no song loaded, the drum track's own pattern) and
  the JAM PROJECTS pages under SETUP > SYSTEM. The drums-only stages and the UI redesign (LIVE, SONGS, SETUP, the
  song screen) left it as it was found, and the user reports it no longer works as it did on SLOOP: what exactly
  (the keys and layers on that page, SAVE held + key for the sections, the chain editor, the project pages, SONG
  REC) is to be established on the device or in `tests/ui_pages_test.c` and `tests/arranger_test.c`, then restored
  or, where a drummer has no use for it, taken out on purpose and said so. **Dormant (decided 2026-10-19):** left
  in as found, a SLOOP leftover, neither fixed nor removed; the user likes the idea of sketching a rough song
  structure on the device alone and will decide later. Not a blocker for sharing BackBeat: say so where it is
  shared (JAM is SLOOP's, unsupported).
- **The second core** (asked 2026-10-16: "is there any advantage we could take from the two cores?"). The AC791N
  has two pi32v2 cores (the SDK's `CPU_CORE_NUM 2`); Felucca runs bare metal on one: the audio interrupt (every
  32-sample block: the sequencer, the drum voices decoding ADPCM in place from flash, the master chain) and the
  main loop (UI, flash, USB). Core 1 idles; the SDK's two-core RTOS is never started. The host's instruction counts
  per sample (tests/regress.c, 2026-10-16): the idle mix 397, with the drums 677, MULDJORD's worst case (every lane
  every step, ratchets) 1630, a synthesised kit's 2425: roughly 30..45 % of a core at 44.1 kHz and 160..240 MHz if
  the pi32v2 retired an instruction a cycle, which it will not with code and samples executed in place from SPI
  flash through the cache: twelve ADPCM streams at once is the real unknown, and only hardware gives it. Decided:
  nothing before the device is measured (the busiest groove with a long crash: dropped blocks, the limiter's
  declick tail; the health checks on a capture). Then, in order: shorter tails on the toms and ride (fewer voices
  ringing), the master chain at a lower rate, and only then core 1, with the master chain (reverb, delay, EQ, the
  limiter: the fixed ~400 instructions a sample, a finished block as its input) as the piece to move there, one
  block (0.7 ms) of latency more; or the main loop on core 1 so a save or a load can never steal the interrupt's
  time. Not the drum voices: both cores share the flash controller and the caches, and the mix is one buffer. The
  cost: bringing core 1 up by hand (its reset vector, stack and interrupt routing from the SDK's bare sources; the
  part the emulator once stalled on), spinlocks and cache maintenance between two caches with no documented
  coherence, and one translation unit of static state read from both sides. Not started.
- **The firmware's own engine in the browser** (from aroum/fm1-custom-fw, 2026-10-16: FM-1 Mobile compiled X0X,
  Felucca and SLOOP to WebAssembly behind a touch UI; fm1-emu runs unmodified firmware in a browser). Route (b) of
  "the drummer without the device", made cheap: the host tests already compile the firmware's sources with a plain
  cc (hostsim.c), so the same sources through emscripten give the web player the real engine (seq.c, pedal.c,
  drums.c, the ADPCM kit) instead of `PLAY` and `playHit`, which reimplement it and must be kept in step by hand
  (the fill rules changed three times in two days; each time both). The page would feed it the pedal's messages
  and take its audio blocks into Web Audio; the kit's WAVs in web/realkit would no longer be needed there. Not
  started; `tests/hostsim.c` is the starting point.
- **A one-switch pedal adapter** (from the same list: pfkellogg's Arduino adapter turns a plain 1/4" footswitch
  into a MIDI CC over TRS). A fill pedal for a guitarist without an Ampero, for a few euros: one switch sending CC
  21 (or two: a section change and the fill), the firmware already listening. To do: a page in docs/ with the
  wiring, a recommended sketch (TRS Type A, the FM-1's jack, verified 2026-10-19) and the CC map;
  nothing in the firmware. Not started.
- **Stage modes for a dark room** (from the official open firmware's "LED illumination modes for dark
  environments"). BackBeat lights the playing section's key on LIVE; a stage mode would raise the LIVE screen's
  contrast and the key LEDs' brightness, dim the menus, and perhaps blink the beat on a LED (the pedal has no
  metronome of its own). A SETUP setting, kept with the pedal's. Not started.
- **To test on the device: USB audio to a DAW.** SLOOP's USB audio out survived the drums-only stages (uac_fir.h,
  usb.c; `tests/uac_test.c`), so the drums should record into a DAW session as a class-compliant stereo device, as
  Melodee's outputs do with take-taker on Android. Verify on hardware with BackBeat's own USB names (fm-static
  exists because renamed USB descriptors confuse the official updater: ours never needs it, but note it in the
  recovery docs), then document it. Not started.
- **The drummer without the FM-1 at all** (the user, 2026-10-16: "exploring the idea of not using the FM-1 at all"; a
  main reason for the FM-1 was its TRS MIDI jack, since the Ampero only sends its footswitch CCs over TRS). The
  laptop or the tablet as the drummer, the Ampero's TRS into a USB MIDI bridge built on a spare STM32F4-Discovery
  (the F407's USB OTG FS port as a class-compliant MIDI device through TinyUSB, a UART at 31250 baud behind an
  optocoupler, H11L1 at 3.3 V, on a TRS jack of the Ampero's type; one direction; a Pico does the same for less).
  What exists: the editor's player follows the device's rules step for step (`PLAY`, kept in step by test_web
  against the firmware's tests), "listen to the pedal" (`PEDALIN`: CC 20..27 and PC from any MIDI input the browser
  sees), the Setlist tab, the LIVE view, humanize, MIDI clock in, the PWA on Android. What it gives: a kit without
  the flash limits (full-length cymbals, every layer and take, 44.1 kHz: the recipe keeps the sources; a hi-fi
  folder for the page is a generator option), unlimited songs, a big screen. What it costs: latency (a laptop with
  an audio interface about 10 ms; Android's Web Audio out 20..100 ms by tablet: measure first), audio out from the
  browser (the jack, or a USB interface on a hub beside the bridge), a laptop on stage with its sleep and update
  hygiene (the PWA on the tablet is more pedal-like), browser audio only after a tap (the Listen button). The FM-1
  keeps: standalone, a speaker, instant boot, a battery, nothing to wake, and that is the point of it: the user's
  idea with the FM-1 is "a simpler setup without smart devices or laptops" (2026-10-16), so this is a second rig
  for when a hi-fi kit or a big screen is worth a laptop on stage, not a replacement, and the firmware stays the
  product. If the second rig is used, "the firmware's own engine in the browser" (WebAssembly) is what keeps the
  two in step, and "a responsive site" matters more. To test, in this order: the bridge on the laptop, Listen, a
  groove, a fill from the Ampero's switch, the latency by ear; then the tablet. Not started.
- (asked and done 2026-10-15, in four stages: the format and the firmware, the packers and the player, the importer,
  the editor) **A fill bar of its own.** Before, a fill was not a separate pattern: it switched on the pattern's fill-only hits
  (and off its not-in-a-fill ones), which the importer put in the phrase's last bar, so a fill asked for in bar 1 of
  a 4-bar verse was heard in bar 4, and a fill's hit on a step the pattern also hits lost its own level. Now a
  section may carry a fill bar of its own, which a fill plays instead of the bar it is in (instead of the pattern's
  last bar until the evening of the same day: "Fills as the arrangers do them"). Decided:
  - **The format**: section flag bit 1 (`SNG_F_FILLBAR`); after the pattern's steps a byte of steps (1..64, one bar
    of the section's meter and division as packed) and those steps in the step form (lanes, a byte a hit: level and
    ratchet, no marks); the section's size covers it (sng.c `sng_walk`, check code 7 for a bad fill bar). In the
    JSON `"fillbar": {"steps": 16, "lanes": {...}, "ratchets": {...}}` (stage 2).
  - **What a fill does** (CC 21, LIVE's black keys, SEL, the editor's f): while it is on, the drum track's steps come
    from the fill bar at the bar's own step (seq.c `fb_dstep`, `drum_src`: step i of the bar is fill step i; the
    phrase cut from the front, as the fill starts where it was asked for), played as they are: no condition, no
    mark. A section with no fill bar fills by its marks as before. (As first done that day the fill bar sat on the
    pattern's last bar and the fill ran to the pattern's end; the arranger model replaced that the same evening.)
  - **The device**: loaded beside the pattern (`fb_len`, 640 B of RAM at most), cleared with the pattern and by a
    song unloaded; the section save (EDIT, SAVE held) keeps it as it is: the device edits the pattern only. Tests:
    song_test.c (checked, loaded, the steps a fill takes, saved back with an edited pattern, cleared).
  - **The editor**: the fill bar as columns of its own after the pattern, a wide amber gap and a FILL label apart,
    amber dashed cells edited with the pen's level and ratchet (no marks there); **Add a fill bar** starts it as the
    pattern's last bar the way a fill played it (its F hits in, its N hits out) and leaves that bar as it plays
    outside a fill (its F hits out), those marks gone (`SED.fbAdd`); **Remove the fill bar** drops it; the bar
    strip's **Fill = selected bar** sets it from the file's bar as it is (`SED.withFill`, which made marks before);
    through split it goes with the last bar, through merge the last part's. The importer emits it instead of the
    marks, losslessly (a hit on a step the pattern also hit keeps its own level now). Tests: test_web.mjs
    (editorFillBar: the packers, the player, the operations), gp2song_test.py.
- (asked 2026-10-15; done 2026-10-16 as CC 21's value) **Play the fill now.** Builds on the fill bar: a command (e.g. CC 28, or some of
  LIVE's black keys) that jumps to the fill bar at once (the rest of this bar from the same beat) or plays it whole
  from the next bar line, the pattern then going on (from where it would have been, or from its top: to decide), a
  section change able to follow it. CC 21 kept as it is (raised 2026-10-14 as "two kinds of fill: now and on the
  next bar"; asked for 2026-10-15). Done: the first half is what CC 21 does since the arrangers' model (the rest of
  this bar from the fill bar, the pattern from its top after); the second is **CC 21 value 1** (asked 2026-10-16:
  "if I press the fill too early the current bar enters fill mode and the fill on the next bar does not play"): the
  whole next bar from anywhere in this one, as a press in the last beat; in a fill, the next bar a fill too; a section
  with it lands on the bar after the fill bar. Any other value is the fill from now. The case: a fill bar whose hits
  lead off the bar (Another Brick in the Wall), which the cut from the front never reaches. On the device, OCT+ held
  with a black key or SEL (the hold the white keys use for "on the next bar"); in the editor, a Fill next bar
  button in the pedal's row (previous, fill, fill next bar, next; the user's layout for the tablet) and the key F,
  and the pedal in says FILL NEXT BAR. Considered first and parked: a rule that defers to the next bar by itself when the
  fill from here would sound exactly like the groove (every lane from the press to the bar line, hits added or
  dropped; a section without a fill bar: its marks). It never makes a fill worse and it costs a scan of 12 lanes
  by 48 steps at press time, but it is a strict diff (one extra ghost note in the fill bar's back half counts as
  "hits ahead"), and the user has an Ampero patch a song, so the switch can say which; the value scheme extends to
  a second fill bar (value 2) if a song ever needs a lead-off and a build-up in the same section, which none does
  yet (two fill bars were discussed and turned down: a format change for a case no song has shown). Tests:
  pedal_test, ui_pages_test (OCT+ + a black key, OCT+ + SEL), test_web (the player, the pedal in's router).
- (done 2026-10-15, superseded the same evening by "Fills as the arrangers do them": a fill is the rest of its bar, or
  the next bar from the last beat) **A fill asked in the last bar is the next time round.** The user's rule: CC 21 (LIVE's black
  keys, SEL, the editor's f) in a pattern's last bar no longer fills the rest of that bar (whatever fill hits are
  still ahead): the next cycle is the fill, all of it (`pedal_fill_wait`, the editor's `fillWait`; a one-bar verse:
  the next bar). A section change asked for meanwhile waits for the fill (live_block holds `pedal_at_end` while a
  fill is pending: chosen with the user), and so does an ending's stop. Asked before the last bar: from now to the
  pattern's end as before (through the next cycle when the fill hits are all behind). LIVE says FILL NEXT TIME
  ROUND while it waits. Checked: pedal_test and song_test (a one-bar section, fill with a change, the 2-bar D), the
  web tests, and both engines on Time's verse and a 4-bar section.
- (asked 2026-10-14; not started) **Per-drum fill marks on the device.** EDIT sets a whole step's fill
  condition (step keys held + OCT+: normal, fill only, no fill; OCT- clears it), but a lane's own marks (the
  song's `"fills"`: `F` only during a fill, `N` never during one; seq.c `drum_fonly` / `drum_fno`) come only
  from the web editor, the importer or a JSON. The device plays them, keeps them through rotate / x2 and the
  section save (SNG2 has the room), and a cleared hit takes its mark with it, but it can neither set nor show
  them. Wanted: a gesture for one lane at a step (e.g. a step key held + the lane's sound chosen + OCT+, cycling
  that hit's mark as OCT+ does a step's) and a mark on the grid where the selected lane's hit has one (the
  step tile's F / x corner, ui_layers.c, is the step's). To check: which lane the gesture means on a step with
  several hits (the selected sound, as SEQ's SOUND knob), the undo, and that the editor's live sync sees it.
- (done 2026-10-14, the user's report on Time's verse; gone 2026-10-15 with the arranger model: no fill carries past
  its bar line) **A fill carries into the next cycle only when it is too late for this one.** CC 21 (LIVE's black keys, SEL, the editor's f) made the fill last to the pattern's end, and through
  the next cycle too when asked in the pattern's last bar: a one-bar pattern (Time's verse) therefore always filled
  twice. Now it carries only when the pattern's fill hits (a step's FILL ONLY, a hit's F mark) are all behind the
  request (pedal.c `pedal_fill_req` from the drum track's step, the editor's `PLAY.fillNext` / `fillHits` alike); a
  pattern with no fill hits keeps the last-bar rule. Checked: song_test (a one-bar section: before its tom, this bar;
  after it, the next bar too), the web tests, and both engines on Time's verse (beats 1-3: this bar; beat 4: the next).
- (done 2026-10-14) **A mid tom instead of the conga.** The lanes renumbered, the toms together: ... SNARE2 8, LOWTOM 9, MIDTOM 10 (GM 45;
  "MID TOM", "tom m"), HITOM 11, CRASH 12, RIDE 13, SHAKER 14, COWBELL 15 (the synthesised kits' lanes likewise: TOMLO 5,
  MIDTOM 6, TOMHI 7 ... SHAKER 10); songs name their lanes, so the JSON stays as it is; the user's songs' tom hits
  were moved onto MIDTOM where their Guitar Pro files play 45 or 47 (each bar matched to the file's: Time, Shine On,
  Back in Black; 25 hits). Why:
  the user's twelve Guitar Pro files had no conga, and five of them (Time, Money, Shine On, Hotel California,
  Hurricane) move between three toms (Money: 33 of its 52 tom changes fell on one lane with two). Every kit: the GM
  toms map 41 43 low, 45 47 mid, 48 50 high (drums.c LANE_OF_GM; congas 62..64 the mid tom, bongos the high: the
  nearest sound, as every note); the synthesised kits' lane 10 (DS_MAP, gen_drumkits.py `midtom`: the low tom's
  sound at the geometric mean of the two toms' pitches, their mean decay; level: the toms' mean trim, so no other
  lane's level and no other golden moved); the GM sample kit plays 45 from its low tom 2 semitones up (as before);
  MULDJORD the Muldjord toms of a 5-piece: Tom4 the floor (69 Hz), Tom2 the mid (98), Tom1 the high (127; it was
  Tom2), budget 290000 -> 312000 B (the image 558952 B, about 22 KB left: the user's choice, "this firmware does
  pretty much everything I want"). gp2song maps the three toms and leaves congas out; fm1_song.py and the editor read
  an old song's "CONGA" as MIDTOM; mkkit, the editor's kit page (KIT_WORDS: mid tom, tom 3) and its served kit
  (web/kit/MIDTOM_A2.wav) follow. Not migrated (no device has them yet): a user kit image with a zone at 63 (its lane
  14 is silent: make it again), a SYN bank's lane 10 (it plays its stored conga sound).
- (done 2026-10-15, see below) **A little random variation per hit (humanize).** MULDJORD's hi-hats (Black
  Pearl, kept by ear over the DRS, Muldjord and Salamander hats auditioned) have one recording a layer, so a run of
  16ths at one level plays the same sample every time. Wanted: each hit of a sampled kit a little different, at no
  flash cost: its level about +-1 dB and its pitch about +-1 % (drum_on: the gain from the voice's velocity, the
  pitch from `v->s[5]`, the step), from the LCG `rk_take` already uses (deterministic renders and goldens). To
  decide: on the hats only or on every lane (the snare and kick already have takes), on MULDJORD only or on the GM
  and user kits too, a SETUP switch or always on; checked by ear on a 16th run, the goldens updated on purpose.
- (done 2026-10-15) **The grooves are the factory songs.** `grooves/` (sixteen generic grooves, `docs/grooves.md`)
  fill the firmware's slots 1..16 in `grooves/factory.txt`'s order (`tools/gen_songs.py`; until then every
  `songs/*.json`, the user's own set, which made the published package depend on what was in that folder). A slot
  plays its groove until the user sends a song to it (the Setlist tab, `--put`); an erased slot stays empty; the
  editor reads a factory song like any slot (editor.c through sng_fetch), so "copy the FM-1's songs into the set"
  takes the grooves too. (Superseded the same day by the two banks below: a user slot never written is empty.)
- (done 2026-10-15) **MIDI clock in: on / off, found where the pedal is.** The FM-1 already followed a clock with
  SETUP > LEARN > SYNC (INT / USB / TRS, G_SYNC); it moved to SETUP > PEDAL as MIDI CLOCK IN (OFF / USB / TRS, KNOB
  4, the same setting and storage: panel.c lights word), LEARN keeps MIDI LEARN and its clear. The web editor's
  Pedal card sets it on the FM-1 (a menu, SET of the SYNC global at once). For the user's rig the Ampero II Stage is
  the master: Global > MIDI > CLOCK OUT(TRS) on (docs/ampero-setup.md). MIDI clock out from the FM-1 (below) is not
  needed for that rig; the FM-1 and the Ampero are both USB devices, so no USB link between them.
- (tried 2026-10-15, turned down) **The ride's bell.** A third ride layer, the bell of the same Zildjian A 24" (Red
  Zeppelin key 53, 0.8 s, 12.8 KB) for the `X` hits, the tip for `x` and `o`. Rendered before / after on RIDE N
  KICK, HALF SHUFFLE, SEVEN and THREE OVER 4: the user preferred the ride as it was (two tip layers), so it was
  reverted (the commit stays in the history: its recipe "bell", gen_realkit.py's support for it, gp2song's bell
  mapping). The 19 KB of the app slot stay free.
- (done 2026-10-15) **Humanize.** SETUP > FEEL (a new section: SWING moved there from TEMPO) > HUMANIZE: OFF, LIGHT
  (the default), MORE; kept in the settings word (panel.c lights word, bits 26..27; a word saved before: off). Each
  step a little early or late (seq.c hum_shift through micro_units, as a nudge: LIGHT +-3 ms, MORE +-6 ms, at most an
  eighth of the step so the steps stay in order; all the lanes of a step together: no flams) and each hit a little
  louder or softer (hum_hit: +-16 / +-32 of 128, about 1 and 2 dB, set for the one hit the sequencer plays next,
  drums.c drum_hum_g, applied in drum_on after the layer is picked: an accent keeps its sample; a hard hit only goes
  down, the voice's velocity is at its top). Triangular spreads from a hash of the step's count (and the lane, the
  ratchet): it varies bar to bar but a render is the same every time. MIDI in, the keys and the auditions play as
  written. The pitch variation of the first idea (+-1 %) was left out: the level and the time carry the feel. Cost:
  about 0.7 KB of code, 16 B of RAM; host CPU +0.1 % at MORE (tests/target_budget.txt: the static count of mix_block
  jumped from inlining, noted there). The host tests run with it off (hostsim host_tracks_init, -DHOST_HUM) except
  seq2's own check (same hits on the same samples, levels within an eighth, timing within 3 ms, repeatable); the web
  player mirrors it (PLAY.humGain / humShift, the same spreads, test_web checks them against seq.c); fm1_render.py
  --humanize (default light). LIGHT chosen by ear on the renders (off / light / more on slow rock, classic rock,
  linear: "light is the best", 2026-10-15), so it stays the default. Verify on hardware: by ear through the FM-1's output.
- (done 2026-10-15) **Two banks: USER and FACTORY.** Sending songs hid the grooves (a slot written no longer played
  its groove), so the grooves became a bank of their own: songs 16..31 (`SNG_FAC0`, `SNG_ALL` 32 in seq.c), read from
  the firmware image (sng_fetch), read-only; the user's 16 slots in flash are empty until written. The pedal: PC 0..15
  the user's slots, PC 16..31 the grooves, PC 32 and on no song (A..D). SONGS has two pages as header tabs, USER and
  FACTORY (ENV / LFO; also on LIVE with no song, whose 16 tiles and white keys follow the page shown); it opens on the
  loaded song's page, else USER, or FACTORY while USER is empty. LIVE's header says "song n" or "factory n"; EDIT's
  SAVE held on a groove answers FACTORY: READ-ONLY (sng_save_section 5). The autoload keeps a groove (last 1..32). The
  editor protocol: objects 48..63 GET the grooves, a PUT is refused (rc 1). The web editor's Setlist tab lists the
  grooves from the site's copy of `grooves/` (offline too, cached by the service worker): play, open in the editor,
  add to the set; the pedal-in player plays PC 16..31 from it. RAM +224 B (`sng_lib` 32 entries), the image +544 B (561728 -> 562272 B with the header hint). Tested: song_test
  (PC 17 a groove, PC 32 none, the autoload's 32), ui_pages_test (the pages, keys, start page, header), test_web.mjs,
  the emulator (boots on FACTORY with no user songs, PC 17 loads BLUES as "factory 2", PC 5 on an empty slot says NO
  SONG 6). Verify on hardware: ENV / LFO on SONGS, objects 48..63 over USB.
- (done 2026-10-15, five stages) **A Setlist tab that works without the device: the tablet as the drummer.** The set:
  up to 16 songs (the FM-1's slots) in the browser's localStorage (`SET`, tested: checked as the device would, kept,
  restored, a set file `{"backbeat_set": 1, "songs": [...]}` to export and import), added from JSON files (a drop on
  the card too) or the Song tab's editor, a row a song (16 fit a tablet), reordered, removed. Played there: the one
  player (`#splay`) moves into the tab that shows; previous / next song (also the arrow and Page keys: a page-turner
  pedal); a song picked while playing loads as a PC (the groove goes on); the pedal's PC n is song n of the set first,
  else the FM-1's slot. Live mode: the stage view (the song, the LIVE view, the tiles and transport, all big; the
  editor's chrome away), full screen, a screen wake lock (asked again when the page comes back); Esc leaves. The
  section tiles were made brighter everywhere (the user's request: hard to read with many sections). With the FM-1:
  "send the set" (`SET.sendPlan`: song n in slot n, the slots after it erased only where they hold a song; stopped)
  and "copy the FM-1's songs into the set". Checked: the web tests (the set, the send on the simulated FM-1) and in
  Chrome driven headless (the set stored and reloaded, a song played from the list, live mode at 1180 x 820 with a
  14-section song, the send on ?mock=1). To check on a real tablet: the wake lock, full screen, latency, the pedal
  through a USB MIDI interface.
- (asked 2026-10-14; not started) **MIDI clock out over USB.** The device follows a clock (SETUP > SYNC = USB /
  TRS, seq.c `mclk_event`) but sends none: MIDI OUT (SETUP > MIDI > OUT: KEYS / SEQ) has no F8 / FA / FB / FC, so
  BackBeat cannot be the master of a DAW, a looper or the Ampero (whose USB port takes a clock in: MIDI IN SOURCE).
  Wanted: with SYNC = INT, 24 pulses a beat from the transport clock (seq.c, sample-accurate: a pulse every
  BEAT_U / 24 units, in the block that crosses it), START when the pedal or PLAY starts (after the count-in, on the
  downbeat), STOP on stop and at an ending's end, on the USB port (usb.c's MIDI IN endpoint: the packets as the
  notes go, CIN 0xF single bytes); a SETUP row (CLOCK OUT: OFF / USB) so it is asked for, off by default. To check:
  the jitter of the USB frames (1 ms) against the pulses, a tempo change by tap or by section (the pulses follow at
  once), never out while SYNC follows an external clock (no loop), and the editor's link sharing the port (SysEx
  between the pulses).
- (done 2026-10-14) **The editor's player follows a MIDI clock**, as the device does with SYNC = USB / TRS: on the
  Play card's MIDI input (the pedal's), the box "Follow its MIDI clock". `PCLK` (next to `PEDALIN`, pure, tested)
  reads the realtime bytes as seq.c `mclk_event`: the tempo every 24 pulses (40..240 BPM), START / CONTINUE from
  the top (the player keeps no place) on the next pulse, the downbeat, STOP; no count-in (the master counts); a
  section pressed while stopped starts on the clock's next beat; the LIVE view says `ext`. The phase: each beat of
  the clock (its Web MIDI time stamp on the audio clock) is matched to the player's own time of that beat
  (`PCLK.timeOf` over the steps scheduled) and the steps not yet scheduled move half the difference, 20 ms at most
  a beat. To check by hand with a real master (the mock has no clock): the lock at 60..200 BPM, a tempo ramp, and the
  output latency (the master's sound against the browser's; an offset setting if it shows).

- (done 2026-10-19, the same evening) **Eye of the Tiger and Money refused by the device** (asked 2026-10-19, evening: both
  "issue errors when sending to device"). The cause: both songs were imported in the browser that day and both carry
  `"kit": 46`, where every song that sends has kit 0. 46 was MULDJORD's index before 2026-10-15, when the GM kits
  went and MULDJORD became kit 0; `gp2song.py convert()` still defaults `kit=46` (the command line's `--kit` default
  was moved to 0, the function's was not, and the editor's import calls `convert()` without a kit). The firmware
  refuses a song whose kit is past the device's kits (`sng.c sng_read`: `kit >= DRUM_KITS`, rc 2, the editor's
  "not a song"). Done: `convert()` defaults to `KIT_DEFAULT` (0) like the command line, gp2song_test.py
  calls it as the editor does and checks the kit, the site rebuilt, the two songs' JSON set to kit 0. Still open: the
  device could clamp an old kit index instead of refusing the song (a song saved before 2026-10-15 has the same 46).
- **Demo songs for newcomers** (asked 2026-10-20: do the songs in `songs/` show a guitarist how the tool is used? No).
  `songs/` cannot be the overview: it is never committed, and every song in it is a transcription of a commercial
  record. It is also narrow (classic rock, mostly Pink Floyd, nearly all 4/4, two of eight finished) and a song file
  shows sections, not how a guitarist plays them. The plan: (1) the factory grooves (`grooves/`, original, already
  shipped) as the range of styles; (2) two or three **original demo songs** built from them, written to show the
  workflow: a verse/chorus rock song with fills into changes, a break and an outro; a 12-bar blues shuffle; a 6/8
  ballad with a count-in; each committed with its **pedal plan** (switch, message, what happens), in the README or a
  `docs/demos.md`, and in the Setlist tab like the grooves; (3) a short video or audio clip of the user playing Back
  in Black from the Ampero (playing over a record is not publishing its transcription). Also noticed:
  `songs/seven-nation(wip).json` holds a copy of Shine On (named SHINE ON YOU, sections P2/P3), not Seven Nation
  Army. (2) done the same day: `songs/` emptied by the user and renamed `song_examples/`, which holds three original
  demo songs (FIRST LIGHT, rock 4/4; BACK PORCH, a blues shuffle on 8T; SLOW TIDE, a 6/8 ballad) covering several
  sections, triplets, fill marks, fill bars, entries, breaks (a stop, the blues stop, a silent bar), flams, ratchets
  and outros, each rendered through the firmware's engine with its plan; the pedal plans are `docs/demos.md`. Not
  in the Setlist tab yet; (3) not started.
- **A crash after a fill** (asked 2026-10-20: "often times a fill is followed by a crash on beat 1 of the next bar").
  Today nothing adds hits to the bar after a fill: `live_block` restarts the pattern from its top when a pedal fill
  ends, but an entry plays only when a section comes in from another one (never on the same section looping, and it
  replaces the bar), and a crash written on the pattern's step 0 would sound on every cycle. The plan: a **landing
  accent** per section, an overlay on step 0 of the first bar after a pedal fill (CC 21, a fill bar or fill mode),
  a lane mask (crash, optionally kick) and a level, **added** to the pattern's own hits. A one-shot flag set in
  `live_block` where the fill ends; the drum track's step-0 read (`drum_src`) ORs the overlay in and clears the flag.
  The section whose bar it is carries it, so a fill into a change crashes the chorus and not the verse. **Only after
  a fill**: a plain section change (the white keys, CC 20, CC 27) gets nothing (the entry stays the tool for that).
  The format: a flags bit (as `SNG_F_ENTRY`, `SNG_F_BREAK`) and a byte or two (old firmware refuses such a song, code
  6; firmware and editor ship together); JSON `"landing": {...}`; the section editor a "Crash after fill" select
  (off, crash, crash + kick); `PLAY` in the web player mirrors `live_block`. Tests: `song_test.c` (a fill back into the
  same section, a fill into a change, no accent without a fill), `test_web.mjs`; the existing renders and
  `tests/golden.txt` do not change. Not started.
- **Your sounds over MULDJORD, lane by lane; kit extras as packs** (asked 2026-10-22, after the ride went to 2.0 s:
  the app slot has 15 KB left, so more of the kit cannot live in the firmware). The biggest reserve is USR3 + USR4,
  160 KB of flash outside the package (the installer never writes them, an update keeps them), already read in place
  by the voices (samples.c `smp_user_scan`: up to 16 zones a slot, a rate each). Today a song picks MULDJORD or
  USR3+4, all or nothing. The plan: a kit **MULDJORD + USR** (a kit number of its own, appended so songs keep
  theirs): a lane with a sound in USR3+4 plays it (USR3 the hard hits, USR4 the soft, as `DRUM_PAIR`), every other
  lane MULDJORD's, its layers and takes. "Kit extras" are then not a mechanism of their own but ready-made packs the
  editor's Drum kit page sends into those slots, the first **long cymbals** (the same Red Zeppelin ride at 4..5 s
  and a longer crash, hard and soft); the page shows which lanes a pack or the user covers. The cost: a lane lookup
  in `drums.c drum_on` and the kit's name (about 1 KB of code), the editor's player following the same rule, a
  pack or two (`tools/gen_realkit.py` rendering into slot images; `sampleio.user_slot`). The limits: a slot sound
  has two layers at most (USR3 / USR4) and no round robins (the slot format has neither: fine for cymbals, a step
  down for a snare); 160 KB in all (a 5 s ride in two layers at 22 kHz is about 110 KB, so long cymbals or a few
  short replacements, not both), shared with the user's own sounds; a fresh install has plain MULDJORD until a pack
  is sent (a step in the setup docs), and a song on MULDJORD + USR sounds as the slots of the unit it plays on (the
  editor warns). Not chosen: a slot format with layers and takes (the editor, the upload tool and the firmware all
  change, for little gain on cymbals), and the installer writing the data region (a riskier update path; not before
  the alpha). Not started.

## Open questions

- (answered 2026-10-19) The FM-1's TRS MIDI type: Type A (the Ampero over a plain stereo cable, milestone 7).
- (answered 2026-10-22, but for the patch change's own test) The Ampero's toggle and patch-change MIDI behaviour:
  `docs/ampero-setup.md`.
- Tempo sync with the Ampero's own drum machine or its tempo: not needed, noted.

## Related programs (noted 2026-10-12)

Nothing on Linux does this workflow as such (sections from a footswitch with the fill played into the
transition, an ending that plays the outro once, songs from tabs): that is the BeatBuddy pedal's, and it
is hardware. The nearest, for comparison or as a fallback:

- **Hydrogen** (native drum machine, kits and a pattern list): MIDI actions bindable to the Ampero's CCs
  (select the next pattern, select and play a pattern, play / stop, tap tempo); in pattern mode a selected
  pattern takes over at the end of the current one. No automatic fill (a fill is another pattern: two
  presses, or a chain), no ending logic; its own song files.
- **Seq66** (live pattern launcher): patterns armed and muted from MIDI, mute groups switch a set at once
  (a section), changes queued to the next bar, a one-shot pattern plays once and mutes itself (a fill).
  No sound of its own: feed sfizz with an SFZ kit from `tools/mkkit.py`. The closest workflow, the most
  wiring (JACK / PipeWire, sfizz, seq66).
- **Giada** (loop machine): MIDI learn per channel, quantised starts; loops per section rather than
  patterns with fill marks.
- **EZdrummer 3, Groove Agent** (commercial, under Wine with yabridge in a DAW): real fill and ending
  transitions triggered by MIDI notes; the only software with BeatBuddy-style logic built in.
- **This firmware on the desktop**: `tests/song_render.c` already runs the engine, sequencer and pedal code
  on the PC (that is what renders the WAVs). An ALSA MIDI input for the Ampero and a real-time audio output
  around the mixer's block call would make it a rehearsal program with the same songs, kit image, pedal map,
  count-in and fill rules as the FM-1: about a day, and the most faithful option until the hardware comes.
  Not started; listed here, not under future work, until the user asks for it.
