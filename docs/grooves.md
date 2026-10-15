# Grooves

`grooves/` holds sixteen generic grooves (2026-10-15), apart from the user's songs in `songs/`: drums to jam,
rehearse or write over, not arrangements of particular songs. They are ordinary song JSON (the format
`tools/fm1_song.py` describes), on kit 46 (MULDJORD), so they load into the web editor's Setlist tab (sixteen fill
the set), go into an FM-1 slot (`python3 tools/fm1_song.py grooves/seven.json --put 3`), or render on the computer
(`python3 tools/fm1_render.py grooves/seven.json --play "VERSE:8 CHORUS:8 VERSE:4" --fills`).

**They are the firmware's factory grooves** (since 2026-10-15): a read-only bank of their own, apart from the 16
user slots. Groove n is the one on line n of `grooves/factory.txt` (the "Slot" column below): row n of the SONGS
screen's FACTORY page, the pedal's PC 15+n (PC 16..31). Songs sent to the device go in the user slots (the web
editor's Setlist tab, `fm1_song.py --put`) and never touch the grooves. The web editor's Setlist tab lists the grooves
too (the site carries a copy of `grooves/`): play one, open it in the Song tab's editor, or add it to the set to edit
it and send it to a user slot.

## How every groove is built

- Two sections, **VERSE** and **CHORUS**, each a phrase of 4 bars (THREE OVER 4: 3 bars).
- The chorus opens with a crash on its first beat.
- The phrase's last bar carries the **fill**, written with fill marks (`"fills"` in the JSON): the fill's own hits
  are F (only in a fill), the groove's hits it replaces are N (not in a fill), the rest plays either way. Without a
  fill the last bar is the plain groove.
- To hear the fill into the chorus: ask for the fill (the pedal's CC 21, LIVE's black keys or SEL, the editor's
  `f`) during bars 1..3 of the phrase, then the chorus; or fill and chorus on one switch. Asked in the phrase's last
  bar, the fill is the next time round, and a change asked meanwhile waits for it (`docs/design.md`).
- Levels: `x` normal, `X` hard, `o` soft, `g` ghost; a few fills use ratchets (`"ratchets"`: 2 = a double stroke).
- The ride is accented, as a drummer plays it (2026-10-15: written flat, 48 identical hits, it sounded robotic): `x` on
  the beat, `o` between (SLOW ROCK `xoo`, BALLAD 6/8 `x.o.o.`, BLUES `x.o` on each triplet beat, the 8th rides
  `x.o.`, PUNK `xo`); where the beat is the bell (`X`: HALF SHUFFLE, RIDE N KICK, SEVEN) the hits between are `o`.
  With the layered kit `o` is the soft sample and `x` / `X` the hard one, so the accent is heard in the sound, not
  only the level. THREE OVER 4 keeps its threes all `X`: every hit there is the start of a group.

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
| 9 | HALF SHUFFLE (`half-shuffle.json`) | 86, 4/4, triplets (8T) | half time: shuffled hats, the one backbeat on 3, ghosts on the middle triplets | the shuffle on the ride with its bell on the beats, the foot hat | a snare-only build, ghosts swelling to four accents |
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
| 16 | RIDE N KICK (`ride-n-kick.json`) | 118, 4/4, 1/16 | only the ride (its bell: the hard hits) and the kick | the ride's bell accents carry the groove over a syncopated kick | the bell on every beat | kick and crash stabs together |

## Where they come from

Written for BackBeat on 2026-10-15 (by the assistant, from the user's brief: "meat and potatoes" grooves, then
non-standard ones to inspire, then directions without the toms in front, then four on the floor, a metal gallop and
a 6/8 ballad). The fill marks follow one rule: in the phrase's last bar, a hit only in the fill bar is F, a groove
hit the fill bar leaves out is N. Tempo, kit, levels and fills are edited like any song (the Song tab's section
editor). They are not transcriptions; any likeness to a record is the style's.
