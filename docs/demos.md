# Demo songs

`song_examples/` holds three demo songs (2026-10-20), written for BackBeat to show what a song can do. They are
original grooves, not transcriptions of any record. Each one is ordinary song JSON on kit 0 (MULDJORD), so you can
open it in the web editor (the From Guitar Pro, MIDI or JSON card, or drop the file), send it to a slot
(`python3 tools/fm1_song.py song_examples/first-light.json --put 0`), or render it on the computer with the
firmware's own engine (the command under each song).

| Song | Style | What it shows |
| --- | --- | --- |
| `first-light.json` FIRST LIGHT | rock, 4/4, 124 BPM | six sections; an entry as a stick count (INTRO) and a crash landing (CHORUS); fill marks in the pattern (INTRO, VERSE, BRIDGE); a fill bar (CHORUS); a 2-bar stop-time break (CHORUS); flams, ghost notes, ratchets (32nds in a 16th grid); a half-time bridge; a one-bar ending |
| `back-porch.json` BACK PORCH | 12-bar blues shuffle, 4/4, 96 BPM | triplets: every section on 8T, 12 steps a bar; a triplet fill bar down the toms (VERSE); the blues stop as a 4-bar break (VERSE); a ride solo crashed in by its entry; fill marks; a two-bar ending |
| `slow-tide.json` SLOW TIDE | ballad, 6/8, 66 BPM | a time signature other than 4/4; shaker and cross-stick dynamics (soft hits); flammed backbeats and a flammed fill bar (CHORUS); a one-bar silent break where the drums drop out (BRIDGE); fill marks (VERSE) |

## The pedal

The messages are the FM-1's (`docs/design.md`, command map), on the pedal channel (16 by default). Section numbers
count from 0 in the CC value: section 1 is value 0. The screens count from 1.

| Message | What it does |
| --- | --- |
| PC n | load song n (slot n + 1) |
| CC 20, value n | go to section n when the current one ends (stopped: start with it, after the count-in) |
| CC 21 | the fill: the section's fill bar to the end of this bar, or, without one, its fill-marked hits until the section ends |
| CC 28, value n | section n through its break (127: the section playing) |
| CC 22, value n | the ending: section n once, then stop |
| CC 23 | stop now |

A fill sent together with a section change (one footswitch sending both) plays the fill, then the change.

## FIRST LIGHT

| # | Section | Bars | What is in it |
| --- | --- | --- | --- |
| 1 | INTRO | 2 | low-tom and kick groove, a crash on the 1. Entry: one bar of stick clicks (rim), the drummer's count. Fill marks: a snare run on the last beat, the toms out |
| 2 | VERSE | 2 | 8th hats with accents, ghost notes, a flam on the first backbeat, a 32nd snare ratchet. Fill marks: snare and toms on the last beat, the hats out |
| 3 | PRE | 1 | snare 8ths building to 16ths and 32nds (ratchets): a one-bar build into the chorus |
| 4 | CHORUS | 2 | ride and backbeat. Entry: the landing bar with a crash. Fill bar: groove, then a 16th run down the toms, the last snare flammed. Break: 2 bars, one hit, silence, a snare pickup back in |
| 5 | BRIDGE | 2 | half time, flammed snare on 3, ride quarters. Fill marks: snare 16ths and toms on the last beat |
| 6 | OUTRO | 1 | the ending (outro flag): one hit, let ring |

A suggested plan, one switch per line:

| Moment | Switch sends |
| --- | --- |
| start the song | CC 20 value 0 (INTRO: the stick count, then the groove) |
| into the verse | CC 21 + CC 20 value 1 (the intro's snare run, then the verse) |
| verse to pre-chorus | CC 21 + CC 20 value 2 |
| into the chorus | CC 20 value 3 (the pre is its own build; the chorus lands with its crash) |
| a fill in the chorus | CC 21 (the fill bar, then the chorus again) |
| the stop | CC 28 value 3 (the chorus through its break: hit, silence, pickup, back in) |
| into the bridge | CC 21 + CC 20 value 4 |
| the end | CC 22 value 5 |

```
python3 tools/fm1_render.py song_examples/first-light.json --fills \
  --play "INTRO:2 VERSE:4 PRE:1 CHORUS:4 VERSE:4 PRE:1 CHORUS:4 BRIDGE:4 !CHORUS:4 OUTRO"
```

## BACK PORCH

Everything is on 8T (8th-note triplets, 12 steps a bar): the shuffle is written, not swung.

| # | Section | Bars | What is in it |
| --- | --- | --- | --- |
| 1 | INTRO | 2 | the shuffle on the hat. Fill marks: a triplet snare pickup on the last beat |
| 2 | VERSE | 2 | the shuffle, a ghost note, a flam. Fill bar: a triplet run down the toms. Break: the blues stop, 4 bars with a hit on each 1 and a snare pickup into the verse |
| 3 | SOLO | 2 | the shuffle on the ride, kick on all four. Entry: crashes the solo in. Fill marks: snare and toms on the last beat |
| 4 | ENDING | 2 | the ending: a flammed triplet run down the toms, crash on the last bar's 1 |

| Moment | Switch sends |
| --- | --- |
| start the song | CC 20 value 0 |
| into the verse | CC 21 + CC 20 value 1 |
| a turnaround fill | CC 21 (the fill bar) |
| the stop-time chorus | CC 28 value 1 (the verse through its break: 4 bars of stops, then the shuffle again) |
| the solo | CC 20 value 2 (its entry crashes it in) |
| the end | CC 22 value 3 |

```
python3 tools/fm1_render.py song_examples/back-porch.json --fills \
  --play "INTRO:2 VERSE:8 !VERSE:2 SOLO:8 VERSE:4 ENDING"
```

## SLOW TIDE

In 6/8 on 16ths, 12 steps a bar. A count-in (the Pedal card, or `--count-in 1`) counts in 6/8.

| # | Section | Bars | What is in it |
| --- | --- | --- | --- |
| 1 | INTRO | 2 | shaker and a soft cross-stick |
| 2 | VERSE | 2 | hat, cross-stick on 4. Fill marks: snare and toms on the last three 8ths, the hat out |
| 3 | CHORUS | 2 | ride, flammed backbeat. Entry: the landing bar with a crash. Fill bar: a flammed run down the toms |
| 4 | BRIDGE | 2 | floor tom and kick. Break: one bar of silence (the drums drop out), then back in |
| 5 | OUTRO | 1 | the ending: a soft crash, let ring |

| Moment | Switch sends |
| --- | --- |
| start the song | CC 20 value 0 |
| into the verse | CC 20 value 1 |
| into the chorus | CC 21 + CC 20 value 2 (the verse's fill, then the chorus's landing bar) |
| a fill in the chorus | CC 21 |
| into the bridge | CC 20 value 3 |
| the drop | CC 28 value 3 (a bar of silence, then the bridge again) |
| the end | CC 22 value 4 |

```
python3 tools/fm1_render.py song_examples/slow-tide.json --fills \
  --play "INTRO:2 VERSE:4 CHORUS:4 VERSE:4 CHORUS:4 BRIDGE:2 !BRIDGE:2 CHORUS:4 OUTRO"
```
