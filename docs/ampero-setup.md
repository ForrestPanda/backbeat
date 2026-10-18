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

**Fills (since 2026-10-15: as an arranger's fill-in).** A section change brings no fill of its own. A switch
with only CC 21 is a fill: the rest of this bar, from the press to the bar line (the section's fill bar of
its own, cut from the front; without one its fill marks), then the pattern again from its top. Pressed in
the bar's last beat, the whole next bar is the fill: press just before the bar you want filled, as on a
Yamaha. Pressed again in the last beat of a fill, the next bar is a fill too: two presses, a two-bar fill. For a fill into a change, put two messages on the switch: CC 21 (value 0) and the CC 20. Their
order doesn't matter: the fill to the bar line, the new section on the bar after it (Auto Fill In on an
arranger). Pressing the switch of the section already playing does nothing.

**CC 21 with value 1 is the fill on the next bar** (since 2026-10-16): the whole next bar from anywhere in
this one, as a press in the last beat; pressed during a fill, the next bar is a fill too. Use it for a song
whose fill leads off its bar (the hits in the fill bar's first beat, as Another Brick in the Wall): a plain
CC 21 pressed early plays the back of the fill bar and never reaches them. Any other value is the fill
from now. With a CC 20 on the same switch, the new section lands on the bar after the fill bar.

A section pressed early in a 4-bar verse (no fill) lets the verse play its four bars and switches after
them; pressed during the fourth bar, the switch is on the next bar. (A 1-bar section switches on the next
bar.) With a fill on the switch it never waits: the fill, then the change.

Variants: CC 27 with the section's number does not wait for the section's end: the section on the next
bar (with CC 21 on the same switch: the rest of this bar the fill); CC 24 / CC 25 go to the next /
previous section (wrapping; stopped: the first / the last), no fill either; CC 26 is tap tempo.

## Entries and breaks: a switch a section

The layout this was made for (2026-10-18): switches 1..5 the song's sections (CC 20, each with its Ampero scene), 6
and 7 start and stop. A silent bar or a break between two sections needs no section and no switch of its own: write it
into the section that comes after it.

- **The entry** (the editor's Entry select, or `"entry"` in the JSON) plays whenever the section comes in from another
  one or from stopped: the switch stays a plain CC 20. Shine On You Crazy Diamond's way from P3-SYNTH into the ride
  groove (the GP file's bars 166-169: toms, a hit, a silent bar, the pickup: 4 bars between two 3-bar phrases) can be
  P3-GUITAR's 4-bar entry, or P3-SYNTH's break with P3-GUITAR pressed during it: the groove from its top at 170.
- **The break** (`"break"`) is a second way into a section, for the same section too: **CC 28** with the section's
  number, or **CC 28 127** for the section playing, which serves every song from one switch. With CC 21 on the same
  switch, the fill first, then the break (Shine On's bar 204 then 205-207). A section with no break plays its entry,
  and with neither a bar of rest.

| Switch | Channel | Command | Data | What the FM-1 does |
| --- | --- | --- | --- | --- |
| 1..5 | 16 | CC 20 | the section's number | that section when the one playing ends, through its entry if it has one |
| 6 | 16 | CC 21, CC 28 | 0, 127 | a fill, then the section playing again through its break |
| 7 | 16 | CC 23 | 0 | stop |

Pressing the switch of the section already playing still does nothing, so a switch with CC 21 and its own section's
CC 20 is a fill in that section (Another Brick's crash fills in the chorus).

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
differently if the song needs it: CC 21 alone for a fill in any section (value 1 when the song's fill
leads off its bar), or CC 20 with the section you jump to most. Songs authored for this layout keep their sections in playing
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
