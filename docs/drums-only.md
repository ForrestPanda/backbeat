# fm1-drummer: the drums-only device (plan, 2026-10-13)

The user's decision (2026-10-13): the FM-1 becomes a drummer only. SLOOP's synth side goes: the three synth
tracks, the twelve engines, voices, the FM6 bank, user presets, arp, chords, scales, the SLICER insert, the
punch FX and live loop recording (REC, free takes). What stays: the drum track and its kits (ACOUSTIC and
its treatments, the SYN kits, the user kits USR2..4), the drum grid and kit pages, SLOOP's sections A..D,
quick chain and song chain, the drummer (pedal, songs, the song screen), the metronome and the pedal's
count-in, MIDI in (drum notes, the pedal, clock) and out (drum notes), USB audio, the master chain (reverb
and delay sends, TEXTURE, DUST, the DJ filter, the limiter), backup, OTA and recovery.

Why: the flash is full (116 bytes left) and every device feature still open needs room. The audit below says
the room is there.

> **Since (2026-10-14).** Done in ten stages as planned. The device's screens were then redesigned around songs
> (README "On the device", docs/design.md 6c): LIVE, SONGS, EDIT and SETUP replace the song screen, the TRACKS
> screen, the visualiser, the GLO pages and layer and the menu that the stages below still mention; and a section
> change brings no fill of its own any more (CC 21 asks for one). The stages are left as they were done.

## What it frees

| Item | Flash | RAM |
| --- | --- | --- |
| The sampled instrument sets (PIANO, BASS, VIBES, HORNS, STRGS, FLUTE, SCRCH) in `SMP_DATA` | ~212 KB of 303 KB (the PERC set, the acoustic kit, stays) | |
| 12 engines, voice.c, FM6 (core, bank, patches), PHYS, the SAMPLE engine's playback | ~60 KB | `eng_arena` 25 KB (pool) |
| Synth paths of the sequencer, mixer (`mix_part*`, track drive, inserts of parts), arp, chords, scales, strum | ~15 KB | voice pools, `drw_*` ~2.3 KB |
| UI pages, graphs, layers of the synth side (ENV, LFO, EDIT, VOICE, SEL, ARP, STEP, PRESETS, USER, FX punch grid, REC screens) | ~25 KB | |
| Editor protocol: PRESET, NAMES, UP_*, FM6_*, synth STEP_SET / TRACK_STEP | ~6 KB | |
| Punch FX | ~8 KB | `punch_ring` 64 KB (pool) |
| SLICER | ~3 KB | `sl_buf` 32 KB (pool) |
| Free takes, REC | ~4 KB | `ft_ev` 1.2 KB |
| Three synth tracks (`NTRK` 4 -> 1), the project format FUN6 -> one track | ~2 KB | ~6.3 KB, and 4 x ~2.9 KB of project slots and autosave buffers |
| Flash objects: user preset banks 0xDC000.. (16 KB), FM6 bank 0xE5000.. (8 KB) | 24 KB of data flash | |

About 300 KB of the 581 KB app slot and well over 100 KB of RAM. The fork stops being rebasable on SLOOP
upstream after this (the README and design.md say so); SLOOP fixes that matter are taken by hand from then on.

## Decisions taken with it (unless the user says otherwise)

- **One track.** `NTRK` goes to 1 and the drum track is track 0, last of all the steps (stage 8). The project
  format becomes "DRM1", the drum track only (no device holds a project yet: FUN1..6 and their converters go).
  As done: FUN4..FUN6 (SLOOP 2.x) are still read, their drum track taken (cheap, and a SLOOP user's projects
  carry over); FUN1..3 went.
- **The chorus bus goes**: only the synth parts feed it (`drums_mix` sends to reverb and delay).
- **Drum rolls (ROLL layer, note repeat on the keys) stay**: cheap, and a live drum gesture. Drum step entry
  on the grid page stays; recording drum hits from the keys in time (REC) goes with live recording.
- **USR1 leaves the kit list** (its flash holds the songs; it always plays silent). Kit indices renumber;
  songs store the kit index, so `tests/example-song.json` and the editor follow. As done: no renumbering (a
  song's or a project's kit would have changed under it); the device's kit knobs and the editor step over USR1,
  the numbers stay.
- **The editor protocol becomes v11** and says it is a drummer; the editor hides Sound, Library, the synth
  Sequencer parts, Tracks and the Samples chopper for it. `INFO` keeps its shape (NENGINES 0).
- **The keyboard map helpers** (`punch_key`, the black keys' table) move out of punch.c into a keymap file:
  the song screen, the grid page and the layers use them.
- **The sample code the kits need** (ADPCM, `smp_zone_t`, the user slots, `SMP_DATA` as the base of user
  zones) moves out of eng_sample.c into `samples.c`; dsp.c is included by felucca.c itself, before drums.c.

## Stages

Each stage is one commit (or a few), builds, and passes the host tests that remain, with the tests of what it
removes taken out in the same commit. The golden renders change where synth renders are dropped
(`GOLDEN_UPDATE=1`); the drum renders (`drums/gm_kit`, the idle mix) must not change until stage 9, which
says why if they do. The CPU and target budgets are re-based where the code they measure goes
(`BUDGET_UPDATE=1`), with `fm1_alnk0_irq` expected to drop.

1. **The sampled instrument sets.** gen_samples.py builds PERC only; the SAMPLE presets and set names go. The
   synth tracks still work with the other engines. Big room at once, small change. (Golden: the SAMPLE
   presets' renders go.)
2. **Punch FX.** The keymap helpers moved first; the LY_FX layer's punch grid, `punch_process`,
   `punch_freeze_feed`, `rev_frz` go. The FX layer keeps its knobs (DJ filter, DUST, TFLT) or goes whole,
   whichever the UI makes simpler. Tests: punch_test out, ui_pages / stress / soak lose their punch parts.
3. **SLICER.** `SL_SLOPE` moves to fx.c (the INSERT uses it), `slicer_drums` becomes a plain `drums_render`,
   the SLICER page and graph go; the P_SL* parameter ids stay until stage 8. Tests: slicer_test out.
4. **Live recording.** REC, free takes, the REC screen and count-in; the pedal's count-in keeps `ci_*`.
   holds_input's REC-hold "clear the track" stays (it is a drum gesture too). Tests: seq2's t_recmode,
   studio_drums' free-take block, ui_pages' REC screens.
5. **Arp, chords, scales, strum, sustain, pitch bend, keyboard pitch maps.** With their pages (SEL, ARP) and
   layer (SCALE). Tests: scale_test out, seq2's t_chords / t_chordplus / t_arp191 / t_sustain.
6. **Synth playback, input and the mixer.** `seq_step`, the synth halves of `seq_ratchets` / `seq_tick` /
   rolls / `input_*` / `key_down` / `key_up`, MIDI routing to parts, `mix_part*`, track drive and the parts'
   inserts, DUCK, `shed_voice`, `engine_block`, the chorus bus. voice.c shrinks to the drum note calls.
   Done together with stage 5 (one commit): the arp, chords and scales sit inside the synth playback paths,
   so neither builds without the other. A section's length (`section_bars`) is the drum pattern's alone,
   the drum screen no longer leaves for a synth track, and FX > FILTER is reachable on the drum track.
   Left for stage 7: `engine_block` (the engines still render nothing but exist), the chorus bus.
7. **Engines, presets, FM6.** engines.c, eng_*, fm6_*, phys_*, upreset.c, editor_fm6.c; samples.c and dsp.c
   included directly; the editor's PRESET / NAMES / UP_* / FM6_* commands and backup objects 6..8; their
   pages (EDIT, ENV, LFO, VOICE, PRESETS, USER) and graphs; the flash objects UPRESET and FM6BANK (storage
   objects renumbered). Tests: fm6_test, upreset_test, the engine renders and CPU entries out; hostsim.c cut
   to the drum track; regress keeps the drum and mix renders.
   Done: the engines, presets, user presets, FM6 and PHYS out; samples.c is what is left of eng_sample.c (the
   drum samples, the user slots); voice.c is the drum track's note-on; the synth pages and graphs out (ENV, LFO,
   SEL, EDIT, ARP tap to nothing), FX > FILTER / INSERT / DLY / REV left; the chorus bus out (only the synth
   parts sent to it: the renders bit for bit); the editor's INFO lists no engine (the drum marker of v10 then
   holds for every track) and its PRESET / NAMES / UP_* / FM6_* commands and backup objects 6..8 are gone. Not
   renumbered after all: a flash sector's header names its object, so the ids of the preset banks and the FM6
   bank stay retired (renumbering would orphan every song and project saved). The menu NOTES item stays: it
   lights the drum hits on the layers.

8. **One track.** `NTRK` 1, `TRK_DRUM` 0, `track_t` trimmed to what the drum track uses, `song.sel` and
   ALGO's track switching gone, the TRACKS screen and the MIX layer for one track, the project format DRM1,
   the editor's Tracks / mixer for one track. Tests: project_test rewritten for DRM1; every test that set up
   synth tracks (song_test, pedal_test, song_audio_test, seq2_test) checks drum patterns instead.
   Done: NTRK 1, the drum track is track 0; track_t keeps the pattern, its locks and the mix state, voice_t the
   drum voice; `song.sel` and `song.solo` gone (one track: MUTE is all), ALGO selects nothing but the lock parameter.
   The parameter and global ids keep their numbers (projects, locks, learned CCs and the editor name them by
   number); the synth ones are unused. DRM1 holds the globals and the drum track; FUN4..FUN6 (SLOOP 2.x) give their
   drum track, FUN1..FUN3 (SLOOP 1.x, before the drum lanes) are not read any more. The TRACKS screen shows the one
   track, the MIX layer is mute / fill / tap with the drums' LEVEL REV DLY PAN on the knobs, GLO > MIDI is one page
   (CH, IN, OTHER, OUT), the visualiser's four voices are the kit's groups. The editor still speaks v10 with one
   track (stage 9 makes it v11).


9. **The editor.** Protocol v11; the dead tabs and modules out of web/editor.html (Sound, Library, synth
   Sequencer, piano roll and SMF, Tracks, the chopper), the mock device and test_web.mjs to match. The
   kit list without USR1.
10. **Docs.** README (what the device is now), design.md (the flash budget again, the open points), SLOOP.md
    kept as SLOOP's manual with a note on top that the synth side is gone from this fork.

Then the device features, on the room: the pedal channel and the count-in on the song screen, bar counts per
section, the slot list with names, and whatever else the hardware shows is needed. (Done 2026-10-14 in the UI
redesign: SETUP > PEDAL, LIVE's bar counts, SONGS.)

## Risks

- **No hardware**: each stage is checked by the host tests, the build's checks and the emulator
  (`tools/fm1_emu.py`); the emulator run of the drummer demo is repeated after stages 4, 7 and 8.
- **Interleaved code** (`key_down`, `seq_tick`, `events_block`, the layers' `is_drum ? .. : ..` branches):
  removed branch by branch, never by rewriting a function whole; the drum renders and the pedal / song tests
  are the guard.
- **The editor and the device must agree** on the protocol version and the project format; both change in
  stages 8 and 9, which go out together.
