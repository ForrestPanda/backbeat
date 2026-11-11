# Changelog

What changed in each version of BackBeat, the firmware and the website (the editor and the installer at
<https://forrestpanda.github.io/backbeat/>). The version is on the device's boot screen and in SETUP > SYSTEM >
ABOUT; the editor shows it next to the connect button.

## 0.9.5 (2026-11-11)

### Firmware

- **Newer FM-1 units save and install.** Units sold from late 2026 (mostly on AliExpress) carry a UC25HQ80 flash chip
  next to the Puya P25Q80 of the first batches. BackBeat, as SLOOP before 2.8.5, knew only the Puya: on a UC25HQ80 unit
  songs stayed in RAM ("flash error") and the update loader refused to install. Both chips are known now (SLOOP
  2.8.5's fix). Not yet tried on such a unit.
- **SNARE2 is another drum now**: the Red Zeppelin kit's Porkpie snare (AVL Drumkits, Glen MacArthur, CC BY-SA 3.0, as
  the ride), brighter and with more wires. It was the same DRS Kit snare as SNARE, a softer layer on another take, so
  the two lanes sounded alike. The main snare, the rim and everything else stay as they were.
- **The battery reading moves again.** The bars at the top of the screen showed the level read at power-on and
  followed only a jump of 32 counts or more, a rounding flaw in SLOOP's smoothing since 2.0; the battery log had the
  same frozen value. The smoothing keeps fractions now, and the console's `status` shows the last raw sample too.
- **The battery on LIVE.** The three-bar icon sits left of BPM in LIVE's header, with USB beside it while a host
  powers the unit. It was only on the parameter pages (FX, MASTER and the others).
- **A tempo per section.** A section can carry a tempo of its own (`"bpm"` on the section in the JSON, the editor's
  BPM box next to the section's time; blank: the song's). It takes effect as the section lands, on the bar, or at
  once when cued from stopped, the count-in clicking at it; the song's tempo comes back with the next section that
  has none. KNOB 1 and the tap then move the section's own tempo, LIVE's "bpm" label amber to say so, and SAVE held
  keeps it with the section; the SECTIONS tiles show it where the meter goes. A song with one needs this firmware
  (0.9.4 and earlier refuse it).
- **The fill at the section's end: CC 21 value 2.** The section plays through to its end, then its fill of its own,
  whole, as extra bars, then the pattern from its top or the section asked for; the screen says FILL AT THE END while
  it waits. A plain CC 21 still plays the fill whole from the next bar line. In a section whose fill is in its marks,
  value 2 acts as value 1: fill mode on the whole next bar, to the section's end. The message when it is pressed
  says FILL AT THE END (it said FILL NEXT BAR).
- **The bar big without the beat.** On LIVE's bar-big layout the beat of the bar no longer sits at the right of the
  big number: with two-digit bars the two ran into each other. The header's beat lights still show the beat; the
  count-in keeps its COUNT label. Pressed while the section's entry or break plays, the fill waits for the pattern's
  end after it. On LIVE the third black key, A#3, is this fill: F#3, G#3 and A#3 are the fill now, on the next bar and
  at the section's end (A#3 was a second F#3).
- **A section's length counts bars.** On the SECTIONS screen KNOB 2 adds or takes a bar of the section's time at its
  division (it moved a step at a time); when a bar is not whole steps at that division, steps as before.
- **255 steps.** A pattern, a fill bar, an entry and a break each hold up to 255 steps (64 before): 15 bars at 1/16
  in 4/4, 31 at 1/8, 7 at 1/32. A song with a section, entry, fill bar or break past 64 steps needs this firmware
  (0.9.4 and earlier refuse it as "not a song"). On the SEQ and GRID screens the first four black keys pick a page
  of 16 steps within the current 64, OCT- / OCT+ walk all the pages, and the overview strip follows the playhead or
  the cursor through the pattern. The division change refuses a pattern that would pass 255 steps (it said 64).
- **The working project is kept in a new format, DRM2.** The autosave of 0.9.4 and earlier (DRM1) and SLOOP 2.x's
  (FUN4..FUN6) are read and converted at the first boot, so the pattern on the device comes back after the update;
  the first autosave then writes DRM2, which an older firmware does not read (after a downgrade the project starts
  plain; the songs are untouched). The editor's backup carries the working project as DRM2.
- **Editor protocol v12.** Step numbers past 127 do not fit a SysEx byte, so the step commands carry two bytes
  (`web/EDITOR_PROTOCOL.md`). The website's editor speaks v12 only: it refuses a unit on 0.9.4 or earlier after
  INFO, as it refused SLOOP, until the unit is updated.
- **The swing is kept when the division changes.** A hit kept its written place on the beat, but the swing moves
  the off-beats of a straight grid: a 1/8 "and" under swing 67 sounds on the triplet, and on 16T it landed on the
  straight step 3 of 6, where no swing applies, so the feel changed. Now a hit the swing moves goes to the new grid's
  step nearest to where it sounds, when the swing brings one within reach (16T's triplet step; and back to 1/8, the
  "and" again); a light swing or none changes nothing. The screen says DIV: SWING KEPT when it happens. The fill bar,
  entry and break follow the same rule, at the save too.

### Website

- **The editor's player** has the new SNARE2 too.
- **The fill at the section's end** in the player: a fifth button on the fill row, the key `g`, CC 21 value 2 from
  the pedal listened to, FILL AT THE END on the state line; the Pedal card lists it.
- **The section's length is a number of bars** in the section editor, a select like the Entry, Fill and Break ones
  (1 to what 255 steps hold). A file whose pattern ends between bars shows its steps there, with the steps box beside
  it to set them.
- **255 steps** in the section editor: the length, the Fill, Entry and Break selects go to what 255 steps hold (15
  bars of 1/16 in 4/4), the LEN control and the drum grid too; merge and the division change refuse past 255 (64
  before). The editor speaks protocol v12 and refuses a unit on 0.9.4 or earlier until it is updated.
- **Any run of the file's bars.** In the section editor the bars of a loaded Guitar Pro or MIDI file can be selected
  as a run of any length that 255 steps hold (15 bars of 4/4 at 1/16), for the pattern, the fill, the entry or the
  break; the limit was 4 bars. The importer's own 4-bar patterns are unchanged.
- **The file's bars are back in the section editor.** Since the bars select of 2026-11-08 the strip of a loaded
  Guitar Pro or MIDI file's bars (pick them as the pattern, the fill, the entry or the break) had vanished: the two
  shared an element id, and the strip's buttons were drawn inside the select. The test now checks every id is unique.
- **The division change keeps the swing** in the section editor, as the device does: under the song's swing a hit
  the swing moves lands on the new grid's step nearest to where it sounds (a 1/8 "and" under swing 67 on 16T's
  triplet step, not the straight one). A dialog says how many hits that places off their written step before the
  change, with the hits that would fall between the steps, if any.
- **A tempo per section** in the section editor (a BPM box next to the time, the song's tempo as its placeholder), in
  the player (the switch as the section lands, the BPM label amber while the tempo is the section's own, the tiles
  showing it) and in `fm1_song.py`; the Guitar Pro and MIDI importers take a tempo change on a section's first bar as
  that section's (they said it was ignored).


## 0.9.4 (2026-11-06)

### Firmware

- **The kit sounds brighter and less like a machine, with no more flash for samples.** The samples are pre-emphasised
  for the device's playback (its interpolation lost 3 dB at 8 kHz and 6 dB at 11 kHz on the kick, snare and toms, 3 dB
  at 10 kHz on the hats and cymbals; half of that is given back, a full correction sounded hissy), encoded with a
  lookahead (about 2 dB less hiss on the crash and ride tails), and the one-take sounds (the hats, the pedal, the toms,
  the ride) no longer play the very same sample twice running: a neighbour velocity layer stands in, at the hit's
  level. With HUMANIZE on, each hit is a few cents off (6 at LIGHT, 12 at MORE). Each drum has its place in the stereo
  picture now, a drummer's view (hats left, floor tom and ride right, kick and snare in the middle), on top of the drum
  track's PAN; the speaker sums it. **SETUP > DRUMS 2 > KIT PAN** turns it OFF, to DRUMMER (the default) or to AUDIENCE,
  the mirror; kept with the settings.
- **Notes on a song and its sections.** Free text written in the editor's Song tab (`"notes"` in the song's JSON, on
  the song and on a section, up to 255 characters each) travels with the song onto the device. LIVE's state line
  shows the section's first line while nothing else is announced; **ENV on LIVE** opens NOTES, the section's note
  and the song's in full (KNOB 1 or ALGORITHM scrolls; KNOB 2 or PRESETS turns to another section's, under its number and
  name, "no note" when it has none: to read ahead, what plays stays as it is, and the screen opens on the section
  playing), ENV again or HOME back. A song with notes needs this firmware; 0.9.3 refuses it. **`@2` in a note is
  section 2's name**: "@" and the section's number (the pedal's, SECTIONS'), shown as the name, so a cue is short to
  write and follows a rename; "@0", a number past the count or "@" alone print as typed.
- **OCT- and OCT+ tapped cue the previous and next section on LIVE** (and on NOTES), what ENV and LFO did. OCT+
  held with a white key is still "on the next bar" (a tap is a release within half a second with no key meanwhile).
  **ENV opens NOTES** (OCT- did, for a day); LFO does nothing with a song, and with none it turns the page of the
  keys' songs, USER or FACTORY.

### Website

- **The editor's player follows**: the same stand-in layers, the same pans, the re-rendered kit.
- **Song notes and section notes** in the Song tab, sent with the song and read back from the device. The player's
  LIVE view shows the section's first line (the song's when it has none) on its state line while nothing is
  announced, as the device does, `@2` shown as section 2's name. Duplicate, New section, Split, Merge and Delete in
  the Song tab renumber the notes' `@n` so they keep pointing at the same section (a deleted section's becomes its
  name). Under each notes field, while the note has an `@`, a preview of it as the device shows it: the names in
  colour, the `@n` as their tooltip, one that names no section struck through.

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
