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
| (on patch select, if the Ampero can send it) | 16      | PC      | the song: 0..15 a slot, 16..31 a groove        | loads the song: tempo, kit, the intro cued                                                                                             |
| 1                                            | 16      | CC 20   | 0                                              | INTRO (or whatever section 0 is): starts the song from stopped (after the count-in's clicks, if that setting is on); playing: when the section playing ends |
| 2                                            | 16      | CC 20   | 1                                              | VERSE when the section playing has played its pattern out (no fill: add CC 21 to the switch for one)                                  |
| 3                                            | 16      | CC 20   | 2                                              | CHORUS                                                                                                                                 |
| 4                                            | 16      | CC 20   | 3                                              | SOLO (set the scene to the lead sound on the same switch)                                                                              |
| 5                                            | 16      | CC 22   | the outro's number (or 127: the flagged outro) | ending: the section playing ends, the outro once, stop on the bar                                                                      |
| 6                                            | 16      | CC 23   | 0                                              | stop now                                                                                                                               |

The data value of CC 20 is the section's number in the song's JSON (0 = the first). `tools/gp2song.py`
prints the numbers; `tools/fm1_song.py song.json` lists them.

**Fills (since 2026-10-14).** A section change brings no fill of its own. For a fill into the change, put
two messages on the switch: CC 21 (any value) and the CC 20. Their order doesn't matter. The fill runs from
the press to the end of the section playing, and the change lands there; the fill ends with it. A switch
with only CC 21 is a fill without a change: from now to the end of the pattern (pressed in its last bar:
through the next cycle too). Pressing the switch of the section already playing does nothing.

A section pressed early in a 4-bar verse lets the verse play its four bars and switches after them;
pressed during the fourth bar, the switch is on the next bar. (A 1-bar section switches on the next bar.)

Variants: CC 27 with the section's number does not wait for the section's end: the section on the next
bar (with CC 21 on the same switch: the rest of this bar the fill); CC 24 / CC 25 go to the next /
previous section (wrapping; stopped: the first / the last), no fill either; CC 26 is tap tempo.

## Five switches

With five switches free for the drummer, a song of any length (up to 16 sections) still fits:

| Switch | Channel | Command | Data | What the FM-1 does |
| --- | --- | --- | --- | --- |
| 1 | 16 | CC 24 | 0 | next section (stopped: the first one starts the song); the sections in song order |
| 2 | 16 | CC 25 | 0 | previous section (back to the verse after the solo, a second chorus) |
| 3 | 16 | CC 21, CC 20 | 0, the chorus's number | the chorus from anywhere, with a fill into it |
| 4 | 16 | CC 21, CC 22 | 0, 127 | ending: a fill to the section's end, the flagged outro once, stop on the bar |
| 5 | 16 | CC 23 | 0 | stop now |

Switches 1 and 2 can carry CC 21 too, for a fill into every change. Switch 3 is the one to spend
differently if the song needs it: CC 21 alone for a fill in any section, or CC 20 with the section you
jump to most. Songs authored for this layout keep their sections in playing
order and flag the outro (`"outro": true` in the JSON; the editor's Outro box).

## A set: patches in song order

With one patch per song and the patch change sending PC n, stepping through the Ampero's patches walks
through the set; the drummer loads each song as its patch comes up (stopped: at once, with the intro
cued; playing: with the next section). Songs live in slots 0..15 (`tools/fm1_song.py song.json --put n`): a set of 16.

## The Ampero's tempo: MIDI clock (for loops in time with the drums)

The Ampero can be the clock master over the same TRS cable (its manual, firmware V1.4.1, Global > MIDI):

| Where | Setting | Value |
| --- | --- | --- |
| Ampero, Global > MIDI | CLOCK SOURCE | Internal (or the default Mixed; TRS Only / USB Only switch the clock out off) |
| Ampero, Global > MIDI | CLOCK OUT(TRS) | On (off by default; CLOCK OUT(USB) can stay off) |
| FM-1, SETUP > PEDAL | MIDI CLOCK IN | TRS (the web editor's Pedal card: MIDI clock in) |

The FM-1 then plays at the Ampero's tempo (LIVE shows `ext`), and its START / STOP start the drums from the top
(no count-in: the master counts) and stop them. The tempo sent is the patch's, or GLOBAL TEMPO if that is on
(Global > Controls), so give each song's patch the song's tempo: the song's own tempo is not used while the clock
runs. MIDI CLOCK IN OFF: the drums keep their own tempo whatever comes in.

The Ampero's looper is free-length (you tap its length; it syncs only to the Ampero's own drum machine, by its
manual), so the clock keeps the tempos together but a loop drifts against the drums by however far the tap was off.
To try on the hardware: tight taps on short loops; or the Ampero's drum machine on at zero volume as the looper's
reference (does the Ampero send START when its drum starts?).

## Things to check once, with the FM-1 on the desk

- A toggle-type switch (an effect slot) may send its messages on both the on-press and the off-press:
  section, ending and stop commands do not mind, next / previous / tap would count twice. If it does,
  use scene switches or OFF-function switches for next / previous / tap.
- Whether a patch change sends a Program Change on a global channel: if it is channel 16, it selects a
  song; if not, add a PC to a footswitch, or send it from the patch's first switch.
- The LIVE screen's footer says what the FM-1 received: "NEXT: CHORUS", "SONG: BLACK", "ENDING: OUTRO",
  "STOP", "NO SONG 3". The LIVE screen shows the section playing, the one coming and the fill.
