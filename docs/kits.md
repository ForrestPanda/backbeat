# Drum kits for the arranger

The device plays one sample per lane (16 lanes), from the paired user slot USR3+4 (about 2 x 7.4 s
of mono 22 kHz ADPCM). `tools/mkkit.py` makes that from a folder of WAVs or from an SFZ kit, as an
image for `tools/fm1_render.py --usrkit` (hear it first) or straight into the FM-1 (`--load`).

## The chosen kit: Muldjord (2026-10-11)

After renders of Another Brick in the Wall with five kits, the user chose the **Muldjord Kit**: it is in
`web/kit/` (ten lanes, the three hi-hats from Black Pearl and the ride from Red Zeppelin at the user's wish, CLAP, CONGA and COWBELL from the
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
(KICK2, CLAP, SHAKER, CONGA, COWBELL) to about 0.3 s before that, so the cymbals stay whole (1.2 s here).
Only the files of `--soft-lanes` (KICK, SNARE, SNARE2, HAT, OPENHAT, PEDAL) are taken as soft layers from a folder.

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
RIDE 51 (Zildjian A 24"), SHAKER 54 (tambourine), CONGA 63 (the low roto tom stands in), COWBELL 56,
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
  `python3 tools/mkkit.py MULDJORD --sfz "../../kits/DrumGizmo.MuldjordKit/DrumGizmo/MuldjordKit/Stereo/DrumGizmo MuldjordKit.sfz" --map PEDAL=42:4 --map HITOM=47 --map LOWTOM=41 --map SHAKER=55 --image build/host/muldjord.img`.
  No clap, conga or cowbell in the kit.
- **DRS Kit** (DrumGizmo team with Jes Eiler of DRS Drums, a handcrafted kit; CC BY 4.0; 754 MB):
  `git clone --depth 1 https://github.com/sfzinstruments/DrumGizmo.DRSKit.git`, then
  `python3 tools/mkkit.py DRSKIT --sfz "../../kits/DrumGizmo.DRSKit/DrumGizmo/DRSKit/Stereo/DrumGizmo DRSKit.sfz" --map LOWTOM=43 --map HITOM=47 --image build/host/drskit.img`. No clap.
- **Salamander Drumkit** (Alexander Holm's own birch stave kit, Paiste and Stagg cymbals; CC BY-SA 3.0;
  360 MB, one overhead mic pair, many round robins): `curl -O http://download.linuxaudio.org/musical-instrument-libraries/sfz/salamander_drumkit_v1.tar.7z`,
  `7z x` then `tar xf`, then `python3 tools/mkkit.py SALAMAND --sfz ".../salamander_drumkit_v1/ALL.sfz" --map KICK=35 --map KICK2=36 --map SNARE=40 --map SNARE2=38 --map RIM=41 --map CLAP=39 --map HAT=42 --map OPENHAT=46 --map PEDAL=44 --map LOWTOM=43 --map HITOM=45 --map CRASH=55 --map RIDE=52 --map COWBELL=47 --map SHAKER=63 --map CONGA=57 --image build/host/salamander.img`
  (its keys are its own; a splash stands in for the shaker, a crash for the conga). All 16 lanes.
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
