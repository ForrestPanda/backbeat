# Installing safely, and getting back

BackBeat replaces the FM-1's firmware. Installing it is at your own risk, but the FM-1 is hard to brick,
and every way back below has been walked on a real unit unless it says otherwise.

## Why the FM-1 is hard to brick

- The update writes only the app area (where the firmware lives) and a staging area for its
  update loader. The flash head (the boot code and the chip's key, the first 16 KB) is never
  written by any installer: not M-VAVE's, not SLOOP's, not BackBeat's.
- The chip has a download mode in its mask ROM (silicon, it cannot be overwritten), where the
  whole flash can be read and written over the same USB cable.
- BackBeat has its own rescue screen, which starts before anything else in the firmware.

What can really go wrong is a firmware that starts but never answers on USB. The steps below go
from the everyday to that case.

## Before the first install: a backup (optional, Linux)

The official firmware package does not hold your unit's own data (its Bluetooth address and
pairing records, a few sectors at the top of the flash). No installer touches those sectors, but
a full 1 MB dump is the only copy of them, and it lets you put the unit back exactly as it was.
It takes about a minute.

You need Python 3, `pip3 install --user mido python-rtmidi`, and kagaimiq's
[jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool) next to this repository:

```
git clone https://github.com/kagaimiq/jl-uboot-tool ../jl-uboot-tool
pip3 install --user -r ../jl-uboot-tool/requirements.txt
```

Then, with the FM-1 on the stock firmware, switched on and connected:

```
python3 tools/fm1_recover.py --softkey     # the FM-1 drops into the ROM's download mode (screen frozen)
sudo chmod 666 /dev/sg0                    # the ROM disk the script names; it is root-only
sh tools/fm1_flash.sh dump fm1-stock.bin   # reads the 1 MB twice and compares
sha256sum fm1-stock.bin > fm1-stock.bin.sha256
```

Keep `fm1-stock.bin` in two places, and do not share it (it holds your unit's Bluetooth identity).
To leave the download mode: switch off, unplug the cable, wait 5 s, plug in, switch on. The stock
firmware starts again, untouched.

## Installing

Use the web installer (Chrome or Edge), or from the command line:

```
python3 tools/fm1_install.py --info                       # what runs now: FM-1_015 is stock V15, FM-1_988 BackBeat
python3 tools/fm1_install.py backbeat-0.9.2.fwsc          # about two minutes
```

Close every other program that uses MIDI first, and keep the cable in. The install runs in two
steps: the running firmware hands over to an update loader, and the loader writes the new
firmware. The loader's write is the only moment a power loss matters, and even then the loader
comes back (below).

## Going back to the official firmware

The installer takes M-VAVE's own package: download FM-1 V15 from m-vave.com (Downloads, PC
Firmware) and install `FM-1.fwsc` as you installed BackBeat. The installer checks it is exactly
V15 before it sends it. M-VAVE publishes **V16** now (2026-10-30): the installer refuses it (it
knows V15 by its hash); use M-VAVE's own updater, M-UPGRADE (Downloads, PC Software), with any
official version, closing every other program that uses MIDI first. SLOOP dropped its in-browser
return to stock as not reliable on every system and points at M-UPGRADE too. The dump and the
dongle below are the way back that needs neither.

## When something goes wrong

### An install stopped half-way

If the cable came out, or the computer slept, during the install: switch the FM-1 off and on
(a real power cycle, not just a restart). If the installer (or `fm1_install.py --info`) then sees an
identity starting with `ota-`, the update loader is waiting: run the installer again, it picks up
where it stopped.

If the FM-1 was switched **off** while the loader was writing (the moment the installer warns
about), the screen can stay dark and nothing show up on USB at all, not even the loader. That
happened once (2026-10-30, 0.9 to 0.9.1): the way back was the dongle below and the app area of the
unit's own dump, then stock, then the install again, all in one sitting. Keep the cable in and the
power on until the installer says done.

### BackBeat does not start, or crashes at start

- After **two failed starts in a row**, BackBeat opens its **BACKBEAT USB RESCUE** screen by itself.
- Or force it: hold **OCT-** alone while switching on. (Until 2026-10-30 this never worked, in
  SLOOP 2.6 and in BackBeat alike: the key matrix only reads while the backlight pin is driven, and
  the early key scan ran before the screen was set up, so the key was never seen; SLOOP 2.7.2 found
  and fixed it, BackBeat took the fix; verified on the first unit the same day: OCT- held while
  switching on brings the BACKBEAT USB RESCUE screen up.)

The rescue screen runs nothing but USB and the updater (no audio, no songs, no settings). Open the
installer and install BackBeat again, or the official V15. It reports itself as `FM-1_000`.

### The installer cannot talk to it, but it still shows up on USB

Put the chip into its ROM download mode, then write the firmware area back from your dump:

- on BackBeat: hold **OCT-** and **OCT+** together for 5 s while stopped (a countdown shows from 2 s;
  letting go cancels it). The screen says UBOOT.
- from the computer: `python3 tools/fm1_recover.py --softkey` (stock, SLOOP and BackBeat all
  honour this SysEx: `F0 22 24 35 7D F7`).

Then, from your dump (Linux, jl-uboot-tool as above):

```
dd if=fm1-stock.bin of=app.bin bs=4096 skip=4 count=143   # the app area, 0x4000..0x92FFF
python3 ../jl-uboot-tool/jluboottool.py "write 0x4000 app.bin"
python3 ../jl-uboot-tool/jluboottool.py "read 0x4000 0x8f000 check.bin" && cmp app.bin check.bin
```

Switch off, unplug, wait 5 s, plug in, switch on: the stock firmware starts (verified on the first
unit). `sh tools/fm1_flash.sh restore fm1-stock.bin` writes the whole 1 MB back instead, head
included; it is only for a dump of the same unit.

### Nothing shows up on USB at all

A firmware that crashes before USB starts cannot be reached from the computer. The way in is a
"key" dongle: a small microcontroller board on the cable's D+ and D- lines that sends the chip's
download key while it powers up. [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter)
does this on a Seeed XIAO RP2040, and also restores the firmware. Then restore as above. The
rescue screen comes first and this is rarely needed; it was, once, after a power-off during an
update (2026-10-30, the STM32F4-Discovery dongle and `fm1_flash.sh restore-app` of the bridge's
tools, the record in the bridge's recovery.md). Check the dongle's board really holds the key
program before the first round.

## Telling the states apart

| On USB | What runs |
| --- | --- |
| MIDI device "FM-1", identity `FM-1_015` | stock V15 |
| MIDI device "Felucca MIDI 1" and an audio device "Felucca", identity `FM-1_988` | BackBeat 0.9.1 and later (SLOOP's USB names, kept on purpose; 0.9 answered `FM-1_985`, which Loom FM-1 had first, and the first 0.9 package SLOOP's `FM-1_900`) |
| MIDI device, identity `FM-1_000` | BackBeat's rescue screen |
| MIDI device, identity `ota-FM-1_...` | the update loader, waiting: run the installer again |
| a USB disk 4c4a:8057 "WL82 UBOOT1.00" (`/dev/sgN` on Linux) | the ROM's download mode |
| nothing | off, or crashed before USB: the dongle |

`python3 tools/fm1_install.py --info` prints the identity.

## What never to do

- Never write the flash head (0x0 to 0x4000) from anything but a dump of the same unit.
- Never install with another MIDI program holding the FM-1's port, and never unplug during the
  loader's write.
