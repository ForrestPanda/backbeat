# Setting up the Ampero II Stage for fm1-arranger

What to put into each footswitch's MIDI settings (Stomp mode; the switch's own function Effect Slot,
Scene or OFF; "FS MIDI settings" on the switch). Channel 16 is the arranger's default pedal channel
(`tools/fm1_song.py --pedal-channel` changes it). Every footswitch can send up to three messages, so
a scene change and a section change share one press.

## The cable

The Ampero's MIDI OUT is a 3.5 mm TRS jack, Type A. The FM-1's MIDI IN is a 3.5 mm TRS jack of
unknown type: try a plain stereo cable first (with the stock firmware, a footswitch sending a note
should play a sound); if it stays silent, a Type A to Type B crossed cable. Set the Ampero's jack to
OUT, not THRU.

## A song: one patch

Make one Ampero patch per song. Its footswitches:

| Switch | Channel | Command | Data | What the FM-1 does |
| --- | --- | --- | --- | --- |
| (on patch select, if the Ampero can send it) | 16 | PC | the song's slot, 0..7 | loads the song: tempo, kit, the intro cued |
| 1 | 16 | CC 20 | 0 | INTRO (or whatever section 0 is): starts the song from stopped; playing: next bar |
| 2 | 16 | CC 20 | 1 | VERSE on the next bar, the rest of this bar a fill |
| 3 | 16 | CC 20 | 2 | CHORUS |
| 4 | 16 | CC 20 | 3 | SOLO (set the scene to the lead sound on the same switch) |
| 5 | 16 | CC 22 | the outro's number (or 127: the flagged outro) | ending: the fill, the outro once, stop on the bar |
| 6 | 16 | CC 23 | 0 | stop now |

The data value of CC 20 is the section's number in the song's JSON (0 = the first). `tools/gp2song.py`
prints the numbers; `tools/fm1_song.py song.json` lists them.

Variants: CC 27 with the section's number asks for it with a whole fill bar first (press it early in
the bar); CC 21 (any value) makes the next bar a fill without changing section; CC 24 / CC 25 go to the
next / previous section, for a two-switch setup; CC 26 is tap tempo.

## A set: patches in song order

With one patch per song and the patch change sending PC n, stepping through the Ampero's patches walks
through the set; the arranger loads each song as its patch comes up (stopped: at once, with the intro
cued; playing: with the next section). Songs live in slots 0..7 (`tools/fm1_song.py song.json --put n`).

## Things to check once, with the FM-1 on the desk

- A toggle-type switch (an effect slot) may send its messages on both the on-press and the off-press:
  section, ending and stop commands do not mind, next / previous / tap would count twice. If it does,
  use scene switches or OFF-function switches for next / previous / tap.
- Whether a patch change sends a Program Change on a global channel: if it is channel 16, it selects a
  song; if not, add a PC to a footswitch, or send it from the patch's first switch.
- The top bar of the FM-1 says what it received: "NEXT: CHORUS", "SONG: BLACK", "ENDING: OUTRO",
  "STOP", "NO SONG 3". The TRACKS screen shows the section playing and, in amber, the one coming.
