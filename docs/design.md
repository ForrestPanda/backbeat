# fm1-arranger: design

A guitarist's accompaniment device on the M-VAVE FM-1. The idea comes from an arranger keyboard,
reduced to what a guitarist with a pedalboard needs: songs made of sections (intro, verse, chorus,
bridge, solo, outro), each a drum loop, switched live from the pedalboard, with fills on the
transitions. No backing tracks: the structure and the length of every section, the solo above all,
are decided on stage.

Base: a fork of [SLOOP](https://github.com/isod89/sloop-fm1) (GPL-3, from Felucca), rebased on
**SLOOP 2.6** on 2026-10-11 (first done on 2.2). SLOOP brings the sample-accurate clock, the drum engine
(synthesised kits, a drum synth, a sampled acoustic kit, your own 16-pad WAV kits from the editor), an
ADPCM sample engine, live sections that switch on the bar, a quick chain, **fills** (per-step
FILL ONLY / NO FILL conditions, "fill while held" and "fill the next bar"), the TRS MIDI jack (driver
fixed and used for MIDI clock since 2.3), per-track MIDI channels, Program Change, MIDI import and
export in the web editor, and the installer.

## The pedalboard

A Hotone Ampero II Stage. Its MIDI OUT is a 3.5 mm TRS jack, Type A (Hotone's quickstart). The FM-1's
TRS MIDI IN type is not documented: **verify** with a plain stereo cable on the stock firmware; if it
stays silent, a Type A to Type B crossed cable.

Each Ampero footswitch sends up to three MIDI messages (CC 0..127 or PC, with a value, on a channel),
only in Stomp mode and only when the switch's own function is Effect Slot, Scene or OFF. Scenes are the
natural pairing: the scene that switches to the lead sound also sends "go to the solo section".

**Verify on the Ampero:** whether a switch assigned to an effect slot sends its messages on both the
on-press and the off-press, and whether a patch change sends a program change on a global channel.
The command map is built so that neither matters.

## Command map (firmware/src/pedal.c)

One channel for the pedal, default 16 (0 = off, 17 = omni). Everything is absolute, so a switch that
repeats its message does no harm; next / previous / tap count presses.

| Message | Meaning | Status |
| --- | --- | --- |
| CC 20, value n | go to section n on the next bar, the rest of this bar a fill; stopped: start the loop with it | done (n = 0..3, A..D) |
| CC 27, value n | the same with a whole fill bar first (the section on the bar after) | done |
| CC 21 | fill the next bar, stay in the section | done |
| CC 22, value n | ending: the fill, section n once as the outro, stop on the bar; n empty or out of range: stop on the next bar | done |
| CC 23 | stop now | done |
| CC 24 / CC 25 | next / previous section, empty ones skipped, wrapping, with the fill | done |
| CC 26 | tap tempo | done |
| PC n | load song n (0..7); 8..127: no song, A..D again | done |

Program change as song select lets one Ampero patch be one song: the patch change loads the song, the
switches inside the patch are its sections. Starting a song is "go to intro".

## The model (target)

- A **song** (done in M3): a name, a tempo, a kit, swing, drum level and reverb, up to 8 **sections**.
  Eight songs on the device.
- A **section** (M3): a name, a drum pattern (1 to 64 steps at any of SLOOP's divisions, 16 lanes with a
  level and a ratchet per hit) with its fill conditions (FILL ONLY / NO FILL steps: the fill is inside
  the pattern, not a second one), and the outro flag.
- A **transition** (done in M2): a section request during bar N plays the fill for the rest of bar N,
  then lands on the new section's downbeat; CC 27 fills all of bar N+1 too and lands on N+2, for a
  request late in the bar. The ending (CC 22) is the fill, the outro once, then a stop on the bar. A
  count-in is not there yet (the intro starts at once from stopped).
- **Bass: deferred** (2026-10-11: drums only for now, to keep the project simple). When wanted, it
  costs little: a section already stores SLOOP's synth tracks, so a bass pattern per section with a
  bass sound works without new code. The chord changes would be written into the pattern, since a
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
   pedal sets `pedal_fill` for the rest of the bar (`fill_now` takes it with the UI's fill flags), CC 27
   waits one whole fill bar, CC 21 arms SLOOP's next-bar fill, CC 22 plays the outro for its bars (as the
   quick chain counts them) and stops on the bar. The outro is whichever section the pedal names: no
   outro flag until sections are named in M3. Still 4 sections, still SLOOP's project slots.
3. **Songs (done, host-tested).** `firmware/src/sng.c`: a packed song (header + up to 8 sections: name,
   length, division, flags, fill conditions, drum steps) in one flash sector per slot, 8 slots with A/B
   copies at 0xA0000..0xAFFFF (the USR1 sample slot's flash, which the arranger gives up: sample uploads
   into USR1 are refused; first put at 0xE7000, which turned out to be USR4's), read in place through XIP
   as the FM6 bank is (static RAM was 5.5 KB from full). PC n loads a song: stopped, tempo, kit and the first section at once; playing, with the next
   section on the bar. While a song is loaded the pedal's sections are the song's; a section is the drum
   track only (the synth tracks keep theirs). The ending without a section takes the flagged outro. The
   pedal channel is a 16-byte object of its own (0xB0000 / 0xB1000), set by the tool; no menu page yet.
   A song may be up to 3800 bytes (a sector's payload; the importer shortens patterns to fit).
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
     Still wanted: a real rock kit's recordings (CC0), two velocity layers are out of reach (a zone per
     lane).
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
   section buttons and keys 1..8 are the pedal, with the fill window, CC 27 (shift), the next-bar
   fill, the ending and stop, as a state machine (`PLAY`) tested in `web/test_web.mjs`. A preview:
   no ADPCM, no limiter, no reverb; `tools/fm1_render.py` stays the reference.
5b. **The section editor (done 2026-10-11).** A card on the Songs tab over the working song (from the
   Guitar Pro import, a JSON file, a slot's Edit, or a new song): the song's name, tempo, kit and
   swing; the sections as chips; for the selected one its name, steps, division, the outro flag, up
   and down, duplicate, new, delete, split at a bar, merge with the next; a grid of 16 lanes by the
   steps with a pen (normal, hard, soft, ghost, erase) and a ratchet, bar and beat lines, and a top row
   for the fill conditions; for a song from Guitar Pro the file's bars as a strip (the importer returns
   every bar as strings at 1/16 and each section's range), 1 to 4 bars picked as the pattern, one as
   the fill by the importer's rule; Play, Send to a slot, Save JSON. The operations are a pure module
   (`SED`: norm, setHit, setCond, setSteps, split, merge, fromBars, withFill...) tested in
   `web/test_web.mjs`; the page only draws and calls them.
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
6. **The screen (done 2026-10-11, first version).** On the TRACKS screen (the one on stage) the title's
   place shows the section playing (the song's name for it, or A..D) and under it the bar it is in,
   grey and "cued" while stopped; the parameter pages' top bar shows the same in the octave's place.
   The pedal's events are said in the top bar for about 1.5 s: "NEXT: CHORUS", "SONG: BLACK",
   "ENDING: OUTRO", "FILL NEXT BAR", "STOP", "NO SONG 3", "SECTIONS A-D". The UI reads only variables
   the sequencer keeps (`sng_ui_name`, `pedal_ui`), so SLOOP's UI tests, which stub the arranger, still
   build. While a request is pending the line under the section shows the one coming, ">SOLO", in
   amber (or ">STOP" for an ending with no outro). Not done: the pedal channel in the menu (no free
   row or tab; the tool sets it), the song's name on screen while playing.
7. **Hardware.** Everything above on a real FM-1: the TRS jack, the cable type, the UART driver, the
   speaker.

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
| SLOOP 2.6 image, and this fork after milestone 3 | 573052 B, 575660 B: about 10 KB of headroom left |
| Data region (0x93000..0xE0000): projects, user presets, 3 user sample slots of 80 KB | about 308 KB |
| Songs (0xA0000..0xAFFFF, 8 slots, A/B) and the pedal settings (0xB0000 / 0xB1000) | 72 KB of USR1's 80 KB; USR2 and the kit pair USR3+4 stay |
| ADPCM sample audio | about 11 KB per second (mono 22 kHz, 4 bit) |

So a general-MIDI-grade kit with many velocity layers does not fit. A drums-only firmware that
reclaims the synth engines, effects and sampled instrument sets, plus the data region, has roughly
400 to 500 KB for drum audio: about 40 seconds. Enough for one good acoustic kit with two layers.
Numbers to **verify** against the linker map once milestone 4 starts. The image already fills the app
area (milestones 1 to 3 cost about 2.6 KB together), and static RAM is at 92808 of 98304 bytes: songs are
therefore read from flash in place, and anything else added before milestone 4 must stay small.

## Fills, mark by mark (2026-10-11, evening)

The user expected to mark each box of the grid as a fill, and that is the better model: SLOOP's fill condition
covers a whole step, every lane at once, which forced the "additive" fills and the dropped hits. The arranger
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

- (done 2026-10-11) **A section editor in the web editor**: see 5b.
- A kit of real rock recordings (CC0), once the user picks one.
- The pedal channel on the device's menu, if a place for it turns up.

## Open questions

- The FM-1's TRS MIDI type (A or B).
- The Ampero's toggle and patch-change MIDI behaviour (above).
- Which CC0 kit to use (candidates in `assets/samples-cc0/KIT`).
- Tempo sync with the Ampero's own drum machine or its tempo: not needed, noted.
