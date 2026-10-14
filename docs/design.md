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
TRS MIDI IN type is not documented: **verify** with a plain stereo cable on the stock firmware; if it
stays silent, a Type A to Type B crossed cable.

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
| CC 20, value n | go to section n when the section playing has played its pattern out (its bars, counted from where it started), no fill of its own; a 1-bar section: the next bar; stopped: start the loop with it. n the section playing, nothing cued: nothing; with an ending pending it cancels the ending, as any section | done (n = 0..3, A..D; at the section's end 2026-10-13; no fill 2026-10-14) |
| CC 27, value n | on the next bar, without waiting for the section's end, no fill of its own | done (no fill bar 2026-10-14) |
| CC 21 | the fill from now until the section's pattern ends (the FILL ONLY hits in, the NO FILL hits out, wherever they sit); asked for in the pattern's last bar: through the next cycle too (a 1-bar section: the rest of this bar and all of the next); the section goes on. Sent with a section change (the same footswitch, either order): the fill into it, ending when the change lands | done (to the pattern's end 2026-10-13) |
| CC 22, value n | ending: the section playing ends (as CC 20), section n once as the outro, stop on the bar; n empty or out of range: the section ends, then stop | done (at the section's end 2026-10-13) |
| CC 23 | stop now | done |
| CC 24 / CC 25 | next / previous section, empty ones skipped, wrapping, as CC 20; stopped (nothing cued): the first / the last | done |
| CC 26 | tap tempo | done |
| PC n | load song n (0..15; 0..7 until 2026-10-14); 16..127: no song, A..D again | done |
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
  level and a ratchet per hit) with its fill conditions (FILL ONLY / NO FILL steps: the fill is inside
  the pattern, not a second one), and the outro flag.
- **Swing** (2026-10-14): a song's `"swing"` is SLOOP's 0..100 (0 straight, 100 MPC 75 %: `seq.c swing_units`, every
  second step of a straight grid that much late). The importer, `fm1_song.py` and the editor wrote 50 (MPC's "50 %",
  straight on the old scale), which the device played as a shuffle (MPC 62.5 %) while the web player ignored swing:
  the default is 0 now, the songs were set to 0, and the web player swings as the device does.
- A section is a whole number of bars of its time signature (2026-10-14, asked when Back in Black's 4-step
  intro in 4/4 looped four times before the verse: the signature rules, the firmware keeps counting bars).
  `tools/fm1_song.py` (`bar_fit`, `tests/song_bars_test.py`) and the editor's section editor (`SED.barFit`) flag one
  that is not and offer the signature whose one bar is the pattern ("1/4" for that intro). A count-in before such a
  section clicks its short bars (one click a bar for "1/4"): to revisit if it feels wrong.
- A **transition** (done in M2; changed 2026-10-13 and 2026-10-14): a section request lets the section playing
  finish its pattern (a 4-bar verse plays its four bars); a 1-bar section switches on the next bar. No fill
  comes with it (asked 2026-10-14: "I don't want to force a fill on every transition"; until then the last
  bar was the fill, and CC 27 put a whole fill bar first): the fill is CC 21's, and a footswitch that wants
  one sends CC 21 with the section, the fill then running to the change. `pedal_at_end` in seq.c: `live_bar` counts the bars
  since the section started, the pattern's bars (`sng_bars` / `section_bars`) are the period. CC 27 does
  not wait: it lands on the next bar, for a change that must come now. The ending (CC 22) waits for the section's end the same way, then the outro once,
  then a stop on the bar (no outro: the section's end, then the stop). SLOOP's own section keys and
  the chain still switch on the next bar: only the pedal waits. A fill request (CC 21, or the section
  playing pressed again, until 2026-10-14) is on the same count (`pedal_fill_hold`): the fill from now to the pattern's
  end, so the fill bar of a 4-bar verse comes where the pattern has it; pressed in that last bar, the
  next cycle too, so a 1-bar section gets the rest of this bar and all of the next. SLOOP's own
  next-bar fill (`fill_arm`, GLO + key 10) is left to its keys.
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
   (objects 16..23, 24). SLOOP's own sections A..D and song screen are untouched and come back with PC 127.
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
   fill, the ending and stop, as a state machine (`PLAY`) tested in `web/test_web.mjs`. A preview:
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
   `web/test_web.mjs`; the page only draws and calls them. (2026-10-12) The import card opens a song JSON
   too, by picker or by a drop on it or on the editor; undo / redo on a history of JSON states (`SED.hist`,
   `mark` after every render, so a redraw is told from a change), keys for the pen, ratchet, mark and
   sections, Delete / Ctrl+C / Ctrl+V on the selected columns, a lane's sound on its name, "Play this
   section"; the player counts in as the device's setting (mirrored from the pedal row, or picked on the
   Play card), with a synthesised click.
5. **Authoring from Guitar Pro (decided and done 2026-10-11, first version).** The user writes songs in
   Guitar Pro 3 / 4. `tools/gp2song.py` (PyGuitarPro) takes the drum track of a .gp3 / .gp4: the measures between the
   score's section markers (Intro, Verse, Chorus...) become the sections, GM drum notes become the 16
   lanes (kick 35/36, snare 38/40, side stick 37 -> RIM, hats 42/44/46, toms 41..50, crash 49/57, ride
   51/59, cowbell 56, shaker 70, congas 62..64, clap 39), velocities become the four levels, a fill
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
   step as 2 bytes of lanes and a byte a hit (level, ratchet, fill mark). A 64-step section of three hits a step
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
   1..8 CC 20, 9..16 CC 27, the black keys F#3 CC 25, G#3 CC 24, A#3 CC 21, C#4 CC 22, D#4 CC 23, F#4 CC 26;
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
     black key is a fill to the pattern's end (CC 21), so a fill into a change is a black key then a white one;
     ENV / LFO step without a fill. The pedal followed the same day: no section change brings a fill (see the
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
7. **Hardware.** Everything above on a real FM-1: the TRS jack, the cable type, the UART driver, the
   speaker. Before the first install: `sh tests/run_tests.sh` green (but for the known `fm1_alnk0_irq`
   budget failure, identical on untouched SLOOP 2.6), then SLOOP's installer page with `build/felucca.fwsc`.

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
| Data region (0x93000..0xE0000): projects, 3 user sample slots of 80 KB (drums only: the user preset banks at 0xDC000..0xDFFFF and the FM6 bank at 0xE5000..0xE6FFF are free) | about 308 KB |
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
player asks for the fill (CC 21, with the change or alone; LIVE's black keys or SEL).

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
- **A more realistic kit** (asked 2026-10-14, now that the flash and RAM are there): longer samples (the cymbals'
  and toms' tails, today cut to fit), more velocity layers than the two of `mkkit.py --layers`, and round robins
  (two or three takes of the snare and the hats, chosen in turn so repeated hits do not machine-gun). Where it
  can live: the app's own flash now has about 325 KB free besides the code (as a built-in kit next to the GM kit,
  from `tools/gen_samples.py`), plus the user slots USR2..USR4 (240 KB) and the freed user-preset and FM6 sectors;
  at about 11 KB a second of ADPCM, that is some 50 seconds of audio for one kit. To do: a kit description with
  layers and takes per lane (drums.c picks the layer from the hit's level, the take in turn; NDRUM voices may need
  to grow for long tails, RAM allows it), the build and the editor's kit page to make one, `docs/kits.md` for the
  sources (the Muldjord Kit has more layers and takes than we use), CPU checked on the host budget and on the device.
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
- (to reflect on, raised 2026-10-14; not decided) **Two kinds of fill: now and on the next bar.** Today a fill
  (CC 21, LIVE's black keys and SEL) is a mode until the pattern's end: the "only in a fill" hits play where the
  song put them, which the importer puts in the pattern's last bar, so in a 4-bar verse a fill asked for in bar 1
  is heard only in bar 4. "Fill mode now" against "from the next bar" would therefore mostly sound the same. What
  a drummer does on a cue is play the fill *bar* when asked: the section's fill bar (its last bar with the fill
  hits) jumped to now (the rest of this bar from the same beat) or played whole from the next bar line, the
  pattern then going on (from where it would have been, or from its top: to decide), a section change able to
  follow it. Two new commands (e.g. CC 28 / 29, or LIVE's black keys split between them); CC 21 kept. Left as is
  for now: reflect on whether it belongs in the future work once the device is played with the pedal.
- (asked 2026-10-14; not started) **Per-drum fill marks on the device.** EDIT sets a whole step's fill
  condition (step keys held + OCT+: normal, fill only, no fill; OCT- clears it), but a lane's own marks (the
  song's `"fills"`: `F` only during a fill, `N` never during one; seq.c `drum_fonly` / `drum_fno`) come only
  from the web editor, the importer or a JSON. The device plays them, keeps them through rotate / x2 and the
  section save (SNG2 has the room), and a cleared hit takes its mark with it, but it can neither set nor show
  them. Wanted: a gesture for one lane at a step (e.g. a step key held + the lane's sound chosen + OCT+, cycling
  that hit's mark as OCT+ does a step's) and a mark on the grid where the selected lane's hit has one (the
  step tile's F / x corner, ui_layers.c, is the step's). To check: which lane the gesture means on a step with
  several hits (the selected sound, as SEQ's SOUND knob), the undo, and that the editor's live sync sees it.
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

## Open questions

- The FM-1's TRS MIDI type (A or B).
- The Ampero's toggle and patch-change MIDI behaviour (above).
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
