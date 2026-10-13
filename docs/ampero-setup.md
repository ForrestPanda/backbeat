# Setting up the Ampero II Stage for fm1-drummer

What to put into each footswitch's MIDI settings (Stomp mode; the switch's own function Effect Slot,
Scene or OFF; "FS MIDI settings" on the switch). Channel 16 is the drummer's default pedal channel
(`tools/fm1_song.py --pedal-channel` changes it). Every footswitch can send up to three messages, so
a scene change and a section change share one press.

## The cable

The Ampero's MIDI OUT is a 3.5 mm TRS jack, Type A. The FM-1's MIDI IN is a 3.5 mm TRS jack of
unknown type: try a plain stereo cable first (with the stock firmware, a footswitch sending a note
should play a sound); if it stays silent, a Type A to Type B crossed cable. Set the Ampero's jack to
OUT, not THRU.

## A song: one patch

Make one Ampero patch per song. Its footswitches:

| Switch                                       | Channel | Command | Data                                           | What the FM-1 does                                                                                                                     |
| -------------------------------------------- | ------- | ------- | ---------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| (on patch select, if the Ampero can send it) | 16      | PC      | the song's slot, 0..7                          | loads the song: tempo, kit, the intro cued                                                                                             |
| 1                                            | 16      | CC 20   | 0                                              | INTRO (or whatever section 0 is): starts the song from stopped (after the count-in's clicks, if that setting is on); playing: when the section playing ends |
| 2                                            | 16      | CC 20   | 1                                              | VERSE when the section playing has played its pattern out, the last bar of it the fill                                                 |
| 3                                            | 16      | CC 20   | 2                                              | CHORUS                                                                                                                                 |
| 4                                            | 16      | CC 20   | 3                                              | SOLO (set the scene to the lead sound on the same switch)                                                                              |
| 5                                            | 16      | CC 22   | the outro's number (or 127: the flagged outro) | ending: the section playing ends (its last bar the fill), the outro once, stop on the bar                                              |
| 6                                            | 16      | CC 23   | 0                                              | stop now                                                                                                                               |

The data value of CC 20 is the section's number in the song's JSON (0 = the first). `tools/gp2song.py`
prints the numbers; `tools/fm1_song.py song.json` lists them.

Pressing the switch of the section already playing asks for a fill (the next bar, as CC 21): the
section switches are fill switches too, so no switch is spent on CC 21.

A section pressed early in a 4-bar verse lets the verse play its four bars and switches after them, the
fourth bar being the fill; pressed during the fourth bar, the rest of it is the fill and the switch is
on the next bar. (A 1-bar section switches on the next bar.)

Variants: CC 27 with the section's number does not wait for the section's end: the rest of this bar
and a whole fill bar, the section on the bar after (for a change that must come now); CC 21 (any
value) is the fill from now to the end of the section's pattern (pressed in its last bar: through the
next cycle too) without changing section; CC 24 / CC 25 go to the
next / previous section (wrapping; stopped: the first / the last); CC 26 is tap tempo.

## Five switches

With five switches free for the drummer, a song of up to eight sections still fits:

| Switch | Channel | Command | Data | What the FM-1 does |
| --- | --- | --- | --- | --- |
| 1 | 16 | CC 24 | 0 | next section (stopped: the first one starts the song); the sections in song order |
| 2 | 16 | CC 25 | 0 | previous section (back to the verse after the solo, a second chorus) |
| 3 | 16 | CC 20 | the chorus's number | the chorus from anywhere; pressed during the chorus: the fill to its end |
| 4 | 16 | CC 22 | 127 | ending: the section ends (its last bar the fill), the flagged outro once, stop on the bar |
| 5 | 16 | CC 23 | 0 | stop now |

Switch 3 is the one to spend differently if the song needs it: CC 21 for a fill in any section, or
CC 20 with the section you jump to most. Songs authored for this layout keep their sections in playing
order and flag the outro (`"outro": true` in the JSON; the editor's Outro box).

## A set: patches in song order

With one patch per song and the patch change sending PC n, stepping through the Ampero's patches walks
through the set; the drummer loads each song as its patch comes up (stopped: at once, with the intro
cued; playing: with the next section). Songs live in slots 0..7 (`tools/fm1_song.py song.json --put n`).

## Things to check once, with the FM-1 on the desk

- A toggle-type switch (an effect slot) may send its messages on both the on-press and the off-press:
  section, ending and stop commands do not mind, next / previous / tap would count twice. If it does,
  use scene switches or OFF-function switches for next / previous / tap.
- Whether a patch change sends a Program Change on a global channel: if it is channel 16, it selects a
  song; if not, add a PC to a footswitch, or send it from the patch's first switch.
- The top bar of the FM-1 says what it received: "NEXT: CHORUS", "SONG: BLACK", "ENDING: OUTRO",
  "STOP", "NO SONG 3". The TRACKS screen shows the section playing and, in amber, the one coming.
