# Grooves

`grooves/` holds sixteen generic grooves (2026-10-15), apart from the demo songs in `song_examples/` (`docs/demos.md`): drums to jam,
rehearse or write over, not arrangements of particular songs. They are ordinary song JSON (the format
`tools/fm1_song.py` describes), on kit 0 (MULDJORD), so they load into the web editor's Setlist tab (sixteen fill
the set), go into an FM-1 slot (`python3 tools/fm1_song.py grooves/seven.json --put 3`), or render on the computer
(`python3 tools/fm1_render.py grooves/seven.json --play "VERSE:8 CHORUS:8 VERSE:4" --fills`).

**They are the firmware's factory grooves** (since 2026-10-15): a read-only bank of their own, apart from the 16
user slots. Groove n is the one on line n of `grooves/factory.txt` (the "Slot" column below): row n of the SONGS
screen's FACTORY page, the pedal's PC 15+n (PC 16..31). Songs sent to the device go in the user slots (the web
editor's Setlist tab, `fm1_song.py --put`) and never touch the grooves. The web editor's Setlist tab lists the grooves
too (the site carries a copy of `grooves/`): play one, open it in the Song editor tab, or add it to the set to edit
it and send it to a user slot.

## How every groove is built

- Two sections, **VERSE** and **CHORUS**, each with a **fill bar of its own** (`"fillbar"` in the JSON, since the
  evening of 2026-10-15; until then the phrase's last bar carried the fill as marks, and the verse was that 4-bar
  phrase). The verse is one bar: the groove, repeated (THREE OVER 4: its 3 bars, the hats meet the bar again only
  then). The chorus is a 4-bar phrase because it opens with a crash on its first beat (ONE DROP's has none: one bar).
- The fill bar is what the phrase's last bar played as a fill: the fill's own hits, with the groove's hits that go
  on under them, and without the ones the fill replaces.
- To hear the fill into the chorus: ask for the fill (the pedal's CC 21, LIVE's black keys or SEL, the editor's
  `f`) early in the bar you want filled, or in the last beat of the bar before (then the whole next bar is the
  fill), then the chorus, which lands on the bar after the fill; or fill and chorus on one switch. A fill is the
  rest of the bar it is asked in, as an arranger's fill-in (`docs/design.md`), and the groove restarts from its top
  after it: in the chorus, that brings its crash back.
- Levels: `x` normal, `X` hard, `s` soft, `g` ghost (the soft one was `o` until 2026-10-15); a few fills use
  ratchets (`"ratchets"`: 2 = a double stroke). A flam (`"flams"`: `f`, since 2026-10-17) is a grace note of the
  same drum, a ghost about 25 ms before the hit; none of the grooves uses one yet.
- The ride is accented, as a drummer plays it (2026-10-15: written flat, 48 identical hits, it sounded robotic): `x` on
  the beat, `s` between (SLOW ROCK `xss`, BALLAD 6/8 `x.s.s.`, BLUES `x.s` on each triplet beat, the 8th rides
  `x.s.`, PUNK `xs`); where the beat is accented hard (`X`: HALF SHUFFLE, RIDE N KICK, SEVEN) the hits between are
  `s`. With the layered kit `s` is the soft sample and `x` / `X` the hard one, so the accent is heard in the sound,
  not only the level. THREE OVER 4 keeps its threes all `X`: every hit there is the start of a group.

## The everyday grooves

| Slot | Groove (file) | Tempo, meter, grid | Verse | Chorus | Fill |
| --- | --- | --- | --- | --- | --- |
| 1 | CLASSIC ROCK (`classic-rock.json`) | 120, 4/4, 1/16 | kick on 1 and the "and" of 3, snare 2 and 4, closed hats in 8ths | kick 1 and its "and", hard backbeat, ride 8ths | snare 16ths on beat 3, then high, high, mid, low tom |
| 2 | BLUES (`blues-shuffle.json`) | 108, 4/4, triplets (8T) | shuffled hats, kick on 1 and 3, snare 2 and 4 | shuffled ride, kick on every beat, hard backbeat | snare triplets on beat 3, a high-mid-low tom triplet on 4 |
| 3 | ROCK BALLAD (`rock-ballad.json`) | 72, 4/4, 1/16 | side stick on 2 and 4, soft hats, kick on 1 and the "and" of 2 | full snare backbeat, hats with an open hat on the "and" of 4 | 16ths down the toms on beat 4 |
| 4 | SLOW ROCK (`slow-rock.json`) | 84 (a dotted quarter of 56), 12/8, 1/8 | triplet hats accented on each beat, kick 1 and 3, snare 2 and 4 | the ride on every eighth, hard backbeat | snare on beat 3's triplet, the toms down beat 4's |
| 5 | PUNK ROCK (`punk-rock.json`) | 180, 4/4, 1/8 | driving 8th hats, kick 1 and 3, snare 2 and 4 | "skank": kick and snare alternating 8ths on the ride | snare 8ths on beat 3, high then low tom on 4 |
| 6 | FOUR FLOOR (`four-floor.json`) | 120, 4/4, 1/16 | a kick on every beat, snare 2 and 4, closed hats on the beats, open on the offbeats | 16th hats, a clap doubling the snare | the kick goes on under a snare build and a quick high-mid-low tom run |
| 7 | METAL GALLOP (`metal-gallop.json`) | 170, 4/4, 1/16 | the gallop in the kick (an 8th, two 16ths), snare 2 and 4, hats 8ths | the kick in 8ths with a gallop pickup into each bar (16ths until 2026-10-15: too busy), a crash on every beat | 16ths around the snare and the three toms, two floor-tom accents to finish |
| 8 | BALLAD 6/8 (`ballad-6-8.json`) | 75 (a dotted quarter of 50), 6/8, 1/16 | side stick on the "4", soft 8th hats accented on 1 and 4, a kick pickup | snare and ride | 16ths from the snare down the toms in the bar's second half |
| 9 | HALF SHUFFLE (`half-shuffle.json`) | 86, 4/4, triplets (8T) | half time: shuffled hats, the one backbeat on 3, ghosts on the middle triplets | the shuffle on the ride, accented on the beats, the foot hat | a snare-only build, ghosts swelling to four accents |
| 10 | TRAIN BEAT (`train-beat.json`) | 152, 4/4, 1/16 | snare 16ths accented on 2 and 4, kick on every beat, the foot hat on 2 and 4 | boom-chick: kick 1 and 3, snare 2 and 4, ride 8ths | the snare accents shift into threes against the beat |
| 11 | ONE DROP (`one-drop.json`) | 76, 4/4, 1/16 | nothing on 1: kick and cross stick together on 3, hats on the offbeats | a snare on 3, a kick pickup, open hats, shaker 16ths | the hole: everything out but one rim click on the "and" of 4 (the band's cue) |

## The idea starters

Each built around one idea to play with; its fill makes the idea heard.

| Slot | Groove (file) | Tempo, meter, grid | The idea | Verse | Chorus | Fill |
| --- | --- | --- | --- | --- | --- | --- |
| 12 | SEVEN (`seven.json`) | 140, 7/8, 1/16 | 7/8 felt as 2+2+3 | accented hats mark the 2+2+3, snare on the second group | the ride on the groups, the kick on every group with a push, the floor tom on the 3 | snare in twos, the toms down the 3 |
| 13 | TOM TRIBE (`tom-tribe.json`) | 112, 4/4, 1/16 | no hats, no snare: the floor tom on a 3-3-2 clave | the floor tom's clave, the mid tom answering, shaker 16ths | opens into a snare backbeat, open hats on the offbeats, the clave in the kick | the clave on the floor tom under mid and high tom runs, a double stroke |
| 14 | LINEAR (`linear.json`) | 98, 4/4, 1/16 | linear funk: no two drums ever together | kick, snare and hats each alone on every 16th, with ghosts | half time: one snare on 3, ghost notes, an open hat to end the bar | ghost doubles, then double-stroke 32nds (ratchets) down the toms |
| 15 | THREE OVER 4 (`three-over-four.json`) | 104, 4/4, 1/16, a 3-bar phrase | hats in groups of three 16ths across 4/4: they meet the bar again after 3 bars | the hats on the threes, the snare on 2 and 4 the anchor | the ride and the kick on the threes, the foot hat on the offbeats | snare accents in threes, ending on the toms |
| 16 | RIDE N KICK (`ride-n-kick.json`) | 118, 4/4, 1/16 | only the ride (its hard hits the accents) and the kick | the ride's accents carry the groove over a syncopated kick | an accent on every beat | kick and crash stabs together |

## Where they come from

Written for BackBeat on 2026-10-15 (by the assistant, from the user's brief: "meat and potatoes" grooves, then
non-standard ones to inspire, then directions without the toms in front, then four on the floor, a metal gallop and
a 6/8 ballad). Written first as 4-bar phrases with the fill as marks on the last bar, converted on 2026-10-15 to a
fill bar of its own (the last bar as a fill played it) with the repeated bars dropped. Tempo, kit, levels and fills
are edited like any song (the Song editor tab's section editor). They are not transcriptions; any likeness to a record is
the style's.
