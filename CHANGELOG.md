# Changelog

What changed in each version of BackBeat, the firmware and the website (the editor and the installer at
<https://forrestpanda.github.io/backbeat/>). The version is on the device's boot screen and in SETUP > SYSTEM >
ABOUT; the editor shows it next to the connect button.

## Unreleased

### Firmware

- **The kit sounds brighter and less like a machine, with no more flash for samples.** The samples are pre-emphasised
  for the device's playback (its interpolation lost 3 dB at 8 kHz and 6 dB at 11 kHz on the kick, snare and toms, 3 dB
  at 10 kHz on the hats and cymbals; half of that is given back, a full correction sounded hissy), encoded with a
  lookahead (about 2 dB less hiss on the crash and ride tails), and the one-take sounds (the hats, the pedal, the toms,
  the ride) no longer play the very same sample twice running: a neighbour velocity layer stands in, at the hit's
  level. With HUMANIZE on, each hit is a few cents off (6 at LIGHT, 12 at MORE). Each drum has its place in the stereo
  picture now, a drummer's view (hats left, floor tom and ride right, kick and snare in the middle), on top of the drum
  track's PAN; the speaker sums it. **SETUP > DRUMS > KIT PAN** turns it OFF, to DRUMMER (the default) or to AUDIENCE,
  the mirror; kept with the settings.

### Website

- **The editor's player follows**: the same stand-in layers, the same pans, the re-rendered kit.

## 0.9.3 (2026-11-01)

### Firmware

- **KNOB 2 scrolls the songs on LIVE**, as ALGORITHM does: the previous or next song of the page chosen on SONGS,
  USER or FACTORY, empty slots skipped. The kit is on PRESETS (KNOB 2 was the kit).
- **F#4, G#4 and A#4 are "on the next bar"**, what OCT+ is, with no fill: held with a white key, the change comes on
  the next bar; tapped alone, the next white key's change does (KEY: ON THE NEXT BAR on the state line; a second tap
  takes it back); tapped while a change waits for the section's end, that change moves to the next bar. The first
  octave's black keys are as before: F#3 the fill, G#3 the fill on the next bar, C#4 the break.
- **The pairs**: C#4 and D#4 are both the break, C#5 and D#5 both the ending. The fill now is F#3 and A#3.

### Website

- **The editor waits for the FM-1 and connects again by itself.** Unplugged, switched off, restarting after an
  install, or gone silent (three requests unanswered and nothing heard for 3 s): the editor lets it go, says so, the
  LED blinks, and it tries again every 1.5 s, quietly, until the device answers, then reads it again. No page
  reload. Stop waiting on the button ends the retries; Connect with no device plugged in waits for one the same way.
  In Firefox too, whose Web MIDI does not report a device unplugged or plugged back: the editor asks for the port
  list again on every retry, opens each port once, and gives up on a port that hangs (a replugged device never
  came back in Firefox before, not even after a reload).

## 0.9.2 (2026-10-30)

### Firmware

- **Two kinds of fill.** A section's fill of its own (extra bars, 1 to 4) now plays whole, from the next bar line, past
  the section's end if fewer bars are left, then the pattern from its top: press in the bar before you want it. A fill
  embedded in the pattern as marks works as before, from the press to the section's end, like an arranger keyboard's.
  Until now a fill of several bars ended on the section's last bar line, so a 2-bar fill asked in the last bar played
  its second bar only. FILL AT THE END is gone from the screen.
- **No stray hit at a bar line.** A bar's first step that humanize or a nudge had pulled a few milliseconds early
  could play the bar before's content: a fill's second bar started with the fill's first step again (a second crash),
  the bar after a fill or a section change doubled a hit. Such a step now waits for the bar line.

### Website

- **The player follows**: a fill of its own whole from the next bar line.
- **Marks or a fill of its own, never both.** Adding a fill to a section folds its fill marks and conditions in; with a
  fill the mark pens are off; a song file that has both is flagged, with Drop the marks beside Merge into the pattern.
- **Fill, Entry and Break = selected bars follow the section's division.** The bars of a Guitar Pro file are 16ths;
  on a 1/8 or 1/4 section they kept 16 steps a bar, so the device and the player read them as twice or four times the
  bars. They are now put on the section's grid; hits that fall between its steps are dropped and listed (set a finer
  division first to keep them). Pattern = selected bars re-grids a kept fill, entry or break to the new 1/16 as well.
- **The grid's top row labels the columns** (ENTRY, PATTERN, FILL, BREAK). Its cells over the pattern used to set a
  step's fill condition (F / N for every hit of the step); a mark belongs on the hit, with the mark pen.

## 0.9.1 (2026-10-30)

### Firmware

- **Sixteen drum voices** (twelve before): cymbal and tom tails ring on under busy fills instead of being cut for the
  next hit. Measured on the unit: the busiest song's worst audio block went from 40 % to 46 % of the time available.
  (Added to the 0.9.1 package on the evening of 2026-10-30; a 0.9.1 installed earlier that day has twelve.)
- **A new package identity, FM-1_988.** 0.9's FM-1_985 was Loom FM-1's first, so the firmware hubs showed a
  BackBeat unit as Loom. The installer moves a unit from 985 to 988 as from any other firmware.
- **D#5 ends the song**: the outro once, then the stop (the pedal's CC 22). The black keys are now fills, the
  break (C#4), the ending (D#5) and the tap tempo (F#5).
- **SEL opens SONGS.** On SONGS it switches between USER and FACTORY. SAVE's tap does nothing now; held, it still saves
  the section into its song. SEL is no longer a fill on LIVE: the black keys and the pedal fill.
- **ALGORITHM scrolls the songs on LIVE**: each detent loads the next or previous song of the page chosen on SONGS,
  USER or FACTORY, skipping empty slots.
- **The rescue key works.** OCT- held while switching on brings up BACKBEAT USB RESCUE (SLOOP 2.6 never read the keys
  that early; fixed as SLOOP 2.7.2 did).
- **Cleaner fades.** The master's rounding keeps the low bits of fading hits (SLOOP 2.8.4). Audible on the unit.
- **MIDI CLOCK OUT** (SETUP > MIDI 2, KNOB 4): the clock, START and STOP on USB while the clock is INT.
- **MIDI clock in steadier**: the tempo is the mean of the last 8 beats.
- **Knobs with a wide range accelerate** when turned fast, a detent at a time when turned slowly.
- **LIVE's other layout** (HOME tapped on LIVE, kept across power-offs): the bar of the section big, the beat beside
  it, the beat lights while nothing is cued, the next section in large letters when one is.
- **SECTIONS** (EDIT tapped on LIVE): a song's shape on the device. Pick a section with KNOB 1 or the white keys, set
  its steps, division and the tempo, copy it after itself (SEL), move it (ENV / LFO), mark it the outro (OCT+), take
  it out (REC held). EDIT on an empty USER slot of SONGS makes a new song there.
- **The HIT knob** on the drum grid: off, ghost, soft, norm, hard on one knob (KNOB 3), the ratchet on KNOB 4.
- **A DIV change keeps the hits on their beat**: 1/16 to 1/8 drops the 16ths between, 1/8 to 1/16 puts an empty step
  between; the length in time is kept.
- **SAVE held saves the song's header too** (tempo, kit, swing, drum level, reverb as the knobs left them), on LIVE as
  well as on EDIT.
- **The ending into an outro with an entry** played the entry and stopped. Fixed: the entry, the outro once, the stop.
- **The battery**: the core idles between screen frames, and the audio path sleeps after 3 s of silence while
  stopped. SETUP > SCREEN > SCREEN OFF turns the screen off after a time.
- **JAM removed**: SLOOP's four project slots, the arrangement and the SAVE layer. Songs are the only thing on the
  device.
- **ABOUT names this repository**, the version and the build date.
- **The crash screen** shows the stack pointer and stays 10 s.
- **Smaller**: the screens and menus compiled for size, 5 KB of flash back for the kit later.

### Website

- **Merge into the pattern** in the section editor: a fill bar is folded back into the pattern as "only in a fill" and
  "not in a fill" marks, so the pattern carries its own fill again.
- **A warning before a DIV change drops hits**, with how many and into which division.
- **A DIV change re-grids the section** in the editor as on the device.
- **The installer finds the update loader** when Windows names it generically, by its handshake; the editor finds the
  FM-1's ports the same way.
- **The browser player** ends an outro with an entry as the device does now.
- **The Jam tab is gone** with the device's JAM.
- **A Cheat sheet tab in the editor**: the FM-1 drawn with what every knob, button and key does, on LIVE and on the
  other screens; also a page of its own, and in the README.
- **This changelog**, linked from the installer and the editor.

## 0.9 (2026-10-24)

The first release. A drummer for guitarists on the M-VAVE FM-1, drums only: a song as sections (intro, verse, chorus,
bridge, solo, outro) switched live from a pedalboard over the TRS MIDI jack, with fills where asked; 16 songs of up
to 16 sections, 16 factory grooves, the muldjord kit with four velocity layers; the LIVE, SONGS, EDIT and SETUP
screens; the tap tempo key; the TRS to USB bridge for a tablet beside the pedalboard; the rescue screen. The
website: the installer (and the way back to M-VAVE's firmware), the editor with the setlist, the section editor, the
player, the Guitar Pro and MIDI import, backups, all usable without internet once opened.
