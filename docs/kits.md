# Drum kits for the drummer

The device plays one sample per lane (16 lanes), from the paired user slot USR3+4 (about 2 x 7.4 s
of mono 22 kHz ADPCM). `tools/mkkit.py` makes that from a folder of WAVs or from an SFZ kit, as an
image for `tools/fm1_render.py --usrkit` (hear it first) or straight into the FM-1 (`--load`).

## The chosen kit: Muldjord (2026-10-11)

After renders of Another Brick in the Wall with five kits, the user chose the **Muldjord Kit**: it is in
`web/kit/` (ten lanes, the three hi-hats from Black Pearl and the ride from Red Zeppelin at the user's wish, CLAP and COWBELL from the
CC0 studio kit), preloaded on the editor's
Drum kit page and played by its Songs tab. Recipe and licence below, under the three more realistic kits.
For the FM-1, the same files make the device image (or the upload with `--load`):
`python3 tools/mkkit.py MULDJORD web/kit --layers --hires HAT,PEDAL,RIM --image build/host/muldjord-full.img`.

**Two layers and 44.1 kHz (2026-10-11, late).** The pair kit now has two velocity layers: USR3 holds the hard
hits of the drums and hats, USR4 their soft hits (ghost and soft levels, `vel <= 72`, `drums.c`) and the
one-layer lanes (crash, ride, the extras); the device looks in the other slot for a lane it does not find,
so SLOOP's split big kit still works. A zone carries its own sample rate, so the closed and pedal hats and
the rim are kept at 44.1 kHz (the rest at 22.05 kHz: twice the flash a second). `tools/mkkit.py --layers
--hires ...` builds it from an SFZ kit (the soft layer from `--soft-layer`, 3 of 5 by default) or from a
folder with `LANE_note-soft.wav` files. The second slot is the tight one: when it overflows, the longest
sound above its floor loses a tenth at a time until it fits (`MIN_S` in `tools/mkkit.py`: crash and ride
keep 1.2 s, open hat and toms 0.5 s, the rest 0.25 s); soft hits are cut to 0.45 s and the rare lanes
(KICK2, CLAP, SHAKER, COWBELL) to about 0.3 s before that, so the cymbals stay whole (1.2 s here).
Only the files of `--soft-lanes` (KICK, SNARE, SNARE2, HAT, OPENHAT, PEDAL) are taken as soft layers from a folder.

## MULDJORD, the built-in layered kit (2026-10-14)

The device's power-on kit and new songs' default (KIT 0, `"kit": 0`, first in the list since the evening of 2026-10-15; 46 before, when the five GM-sampled kits ACOUSTIC..DUST still led the list): the same mix as above (Muldjord kick and toms, the DRS Kit's snare,
Black Pearl's hats, Red Zeppelin's ride, CC0 clap and cowbell), now with velocity layers and round robins,
built into the firmware (399 KB of the app's flash since 2026-10-22, the ride 2.0 s; 385 KB from the evening of 2026-10-15,
290 KB before: the five GM-sampled kits' 86 KB went to the crash), so the user slots USR3+4 stay yours.

| Lane | Source | Layers x takes | Length | Rate |
| --- | --- | --- | --- | --- |
| KICK | Muldjord 36 | 4 x 2 (the softest 1) | 0.5 s | 22050 |
| SNARE | DRS Kit 38 (chosen by ear from six, 2026-10-14) | 4 x 3 | 0.6 s | 22050 |
| HAT | Black Pearl 42 | 4 x 1 | 0.3 s | 32000 |
| OPENHAT, PEDAL | Black Pearl 46, 44 | 2 x 1 | 0.8, 0.3 s | 32000 |
| RIM | DRS Kit 37 (the same snare's rim; its close mic only since 2026-10-21: the wires and the room made it a roll) | 2 x 1 | 0.4 s | 32000 |
| LOWTOM, MIDTOM, HITOM | Muldjord 41, 47, 48 (Tom4 the floor 69 Hz, Tom2 98 Hz, Tom1 127 Hz: a 5-piece's) | 2 x 1 | 1.0, 0.95, 0.9 s | 22050 |
| CRASH | Muldjord 49 | 1 x 2 | 3.2 s (one take of 2.0 s until 2026-10-15) | 32000 |
| RIDE | Red Zeppelin 51 | 2 x 1 | 2.0 s (1.5 s until 2026-10-22: cut while it still rang, 16 dB under the hit) | 32000 |
| KICK2 | Muldjord 35 | 1 | 0.45 s | 22050 |
| SHAKER | CC0, VSCO-2 CE's shaker (a ride bell until 2026-10-21) | 1 | 0.3 s | 22050 |
| SNARE2 | DRS Kit 38 (layer 3, another take) | 1 | 0.5 s | 22050 |
| CLAP, COWBELL | CC0 | 1 | 0.35 s | 22050 |

A hit's level picks the layer (hard, normal, soft, ghost; a MIDI note's velocity the nearest layer), the take is
another than the drum's last. Twelve voices: at most four of one drum at once (its oldest gives way to its new hit: a
ride on 8ths would otherwise hold half of them), and with none free the quietest gives way (its velocity by the part
of its sample left), not the oldest (until 2026-10-22: often a crash or ride tail still loud). The recipe is `tools/realkit.json`; `python3 tools/gen_realkit.py` renders it from the
source kits (in `fm1-mvave/kits`, decoded by sox) into `assets/realkit/` (a WAV a take, `kit.json`,
`ATTRIBUTION.txt`), which the firmware build encodes and the editor's player plays. The layers are levelled on the
velocity curve x 1.5 (`"dyn"`), the sources' own range being too wide on a small speaker; `"budget"` is the bytes of
ADPCM (the fit shortens the longest sounds above their floor); `--report` lists what the folder holds. To hear it:
`python3 tools/fm1_render.py song.json --kit 0`.

Credit when sharing: "MuldjordKit by Lars Muldjord / DrumGizmo and the DRSKit's snare by DrumGizmo / DRS Drums, CC BY
4.0; hi-hats from AVL Drumkits Black Pearl and the ride from Red Zeppelin, by Glen MacArthur, CC BY-SA 3.0; clap
and cowbell from Versilian Studios, CC0".

**The crash, and the hats left alone (2026-10-15, evening).** With the GM kits out of the image (`docs/design.md`),
the recipe's budget went from 312 to 412 KB: the crash has two takes and rings 3.2 s, and the snare, open hat and ride
have the recipe's full lengths (0.6, 0.8, 1.5 s) instead of what the old fit left them. The older lanes' lengths are
floors (`"min"`) so a tighter fit would only trim the new material. Round-robin hats were tried the same evening from
the Muldjord kit's own hi-hat (Black Pearl's has one hit a layer, no round robins) and reverted: the user prefers
Black Pearl's sound. 32 KB of the slot is left.

## Red Zeppelin (AVL Drumkits), the first rock kit tried

A Ludwig kit with Sabian, Zildjian and Paiste cymbals and a Porkpie snare, recorded by Glen MacArthur
(Bandshed / AV Linux): 44.1 kHz 16-bit mono, five velocity layers per piece, in FLAC with SFZ mappings.
Licence **CC BY-SA 3.0**: free to use, change and share with credit, derivatives under the same licence.
The repository is not part of this project; the kit image is built locally:

```
mkdir -p ../../kits && git clone --depth 1 https://github.com/studiorack/avl-drumkits.git ../../kits/avl-drumkits
python3 tools/mkkit.py REDZEP --sfz ../../kits/avl-drumkits/Red_Zeppelin_5pc.sfz --image build/host/redzep.img
python3 tools/fm1_render.py songs/black.json --usrkit build/host/redzep.img --listen
python3 tools/mkkit.py REDZEP --sfz ../../kits/avl-drumkits/Red_Zeppelin_5pc.sfz --load      # into the FM-1
```

What the tool takes from the SFZ (`SFZ_LANES` in `tools/mkkit.py`): the hardest layer of each GM key,
KICK 36, RIM 37 (side stick), SNARE 38, CLAP 39, SNARE2 40 (the snare's edge), HAT 42, PEDAL 44,
OPENHAT 46 (semi-open), LOWTOM 43 (the 18" floor tom), HITOM 48 (the 14" tom), CRASH 49 (Zildjian A 20"),
RIDE 51 (Zildjian A 24"), SHAKER 54 (tambourine), MIDTOM 45 (the lane was CONGA 63 until 2026-10-14), COWBELL 56,
and KICK2 as the kick's third layer (a softer hit). Black Pearl: the same with `Black_Pearl_5pc.sfz`.

Credit to give with the kit: "AVL Drumkits by Glen MacArthur (bandshed.net), CC BY-SA 3.0". (It was the
served kit until the Muldjord Kit replaced it.)

## Three more realistic kits (2026-10-11, to be judged by ear)

Built the same way, from `../../kits/` next to the project; renders of Black with each are in `build/host/`
(`black-muldjord.wav`, `black-drskit.wav`, `black-salamander.wav`, next to `black-redzep.wav` and
`black-blackpearl.wav`). The tool reads these bigger kits in full: nested includes, ARIA macros, round
robins (the first one), and the several microphones of a hit summed with their place (the drum's own
mics full, overheads -3 dB, ambience -6 dB, the other drums' mics -14 dB, a trigger track left out).
`--sfz-keys` lists what a kit holds, `--map LANE=KEY[:layer]` picks a key by hand.

- **Muldjord Kit** (DrumGizmo team, Lars Muldjord; a Tama Superstar, "a metal or rock kit"; CC BY 4.0;
  347 MB): `git clone --depth 1 https://github.com/sfzinstruments/DrumGizmo.MuldjordKit.git`, then
  `python3 tools/mkkit.py MULDJORD --sfz "../../kits/DrumGizmo.MuldjordKit/DrumGizmo/MuldjordKit/Stereo/DrumGizmo MuldjordKit.sfz" --map PEDAL=42:4 --map HITOM=48 --map MIDTOM=47 --map LOWTOM=41 --map SHAKER=55 --image build/host/muldjord.img`.
  No clap or cowbell in the kit.
- **DRS Kit** (DrumGizmo team with Jes Eiler of DRS Drums, a handcrafted kit; CC BY 4.0; 754 MB):
  `git clone --depth 1 https://github.com/sfzinstruments/DrumGizmo.DRSKit.git`, then
  `python3 tools/mkkit.py DRSKIT --sfz "../../kits/DrumGizmo.DRSKit/DrumGizmo/DRSKit/Stereo/DrumGizmo DRSKit.sfz" --map LOWTOM=43 --map HITOM=47 --image build/host/drskit.img`. No clap.
- **Salamander Drumkit** (Alexander Holm's own birch stave kit, Paiste and Stagg cymbals; CC BY-SA 3.0;
  360 MB, one overhead mic pair, many round robins): `curl -O http://download.linuxaudio.org/musical-instrument-libraries/sfz/salamander_drumkit_v1.tar.7z`,
  `7z x` then `tar xf`, then `python3 tools/mkkit.py SALAMAND --sfz ".../salamander_drumkit_v1/ALL.sfz" --map KICK=35 --map KICK2=36 --map SNARE=40 --map SNARE2=38 --map RIM=41 --map CLAP=39 --map HAT=42 --map OPENHAT=46 --map PEDAL=44 --map LOWTOM=43 --map HITOM=45 --map CRASH=55 --map RIDE=52 --map COWBELL=47 --map SHAKER=63 --map CONGA=57 --image build/host/salamander.img`
  (its keys are its own; a splash stands in for the shaker, a crash on the lane that was the conga's: map MIDTOM to a tom now). All 16 lanes.
- **Black Pearl** (AVL Drumkits, the Pearl kit; CC BY-SA 3.0, in the same download as Red Zeppelin):
  `python3 tools/mkkit.py BLKPEARL --sfz ../../kits/avl-drumkits/Black_Pearl_5pc.sfz --image build/host/blackpearl.img`.

Bigger ones not fetched: Naked Drums (Wilkinson Audio, Yamaha Recording Custom, CC BY 4.0, 1.3 GB,
10 round robins), SM Drums (public domain, 2.2 GB), Karoryfer's Big Rusty Drums (CC0, 2.3 GB, a vintage
kit) and Gogodze Phu Vol II (CC0, 133 MB). The sfzinstruments drums index lists them all.

## The studio kit (CC0)

`assets/samples-cc0/KIT`, SLOOP's own recordings (Versilian Studios VSCO-2 CE and VCSL, CC0):
`python3 tools/mkkit.py STUDIO assets/samples-cc0/KIT --image build/host/studio-kit.img`. Thirteen
lanes; no second kick, no pedal hat, no ride.

## Other free kits looked at (2026-10-11)

Salamander Drumkit (Alexander Holm, CC BY 3.0: many round robins, SFZ, on linuxaudio.org and GitHub),
wavbvkery's 400+ acoustic samples (a vintage Ludwig, "royalty-free"), DrumThrash's free pack (no
licence text found), the Open Source Drum Kit (Real Music Media, called public domain in 2011, no
longer downloadable).
