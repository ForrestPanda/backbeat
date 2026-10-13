#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""fm1-drummer on the FM-1 emulator (fm1-emu, checked out next to my_fw): songs imported into its flash, the
firmware run with a script of keys, knobs and pedal MIDI into the TRS jack, the screen saved at the times asked
for, and the sound.

    tools/fm1_emu.py "songs/back-in-blac(wip).json"                 # the demo script, the song in slot 1
    tools/fm1_emu.py songs/black.json songs/time.json --wav out.wav   # slots 1 and 2, and what it played
    tools/fm1_emu.py song.json --keys "3000:SAVE,3800:n0,4600:PLAY,5600:midi:bf1401" --snap 3600,6000 --ms 7000

The emulator has no USB, so songs cannot be sent to it as to a device. Instead they are written into the flash
image of a copy of build/felucca.fwsc (./build.sh first) as the device's storage writes them (storage.c: the
song objects at 0xA0000.., copy A, a valid header), and the unchanged firmware finds them there at boot, as
it would after tools/fm1_song.py --put. --pedal-channel and --count-in write the pedal settings object too.
The copy is build/emu/felucca-emu.fwsc: for the emulator only.

The demo script: SAVE tapped (the song screen), white key 1 (song 1 loaded), PLAY (its first section), the
pedal's CC 20 1 (the second section when the first ends), A#3 (a fill), the pedal's CC 22 (the ending).
Keys (fm1-emu --keys): t:NAME presses a button (PLAY, REC, SAVE, HOME...) for 150 ms; t:nK the key K (0..26 from
F3: the white keys 0 2 4 6 7 9 11 12.., the black ones 1 3 5 8 10..); t:K1:+3 turns KNOB 1; t:midi:bf1401 sends
bytes into the TRS jack (bf = a CC on channel 16, the pedal's by default). Times are the emulator's ms; it runs
about 30 times slower than the device on a PC without its JIT. Screens: build/emu/lcd_<ms>.png."""
import argparse
import json
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import fm1_song  # noqa: E402
import fm1pkg_make  # noqa: E402

EMU = ROOT.parent.parent / "fm1-emu" / "target" / "release" / "fm1-emu"
DEMO_KEYS = "3000:SAVE,3800:n0,4600:PLAY,6000:midi:bf1401,9000:n5,11000:midi:bf167f"
DEMO_SNAP = "3600,4400,6400,9400,11400,15000"
# storage.c: the objects' numbers (OBJ_SONG0.., OBJ_ARRSET), sectors, the header
ST_MAGIC, ST_SECTOR, ST_PAYLOAD_OFF = 0x554C4546, 4096, 256
OBJ_SONG0, OBJ_ARRSET = 9, 17
SONG_BASE = 0xA0000


def st_object(obj, payload):
    """a storage object's copy A as storage.c writes it: the header (its own CRC last), the payload at 256"""
    hdr = struct.pack("<IHHIIIII", ST_MAGIC, obj, 0, 1, len(payload), zlib.crc32(payload), 0, 0)
    sec = bytearray(b"\xFF" * ST_SECTOR)
    sec[0:32] = hdr + struct.pack("<I", zlib.crc32(hdr))
    sec[ST_PAYLOAD_OFF:ST_PAYLOAD_OFF + len(payload)] = payload
    return bytes(sec)


def unpack_fwsc(pkg):
    """build/felucca.fwsc -> (flash image, update loader, product) as tools/fm1pkg_make.py made them"""
    logical, product = bytearray(), ""
    for i in range(20):
        logical += pkg[i * 0x30:i * 0x30 + 0x2F]
        m = pkg[i * 0x30 + 0x2F]
        if m != 0x7D:
            product += chr((m - i - 1) & 0xFF)
    logical += pkg[20 * 0x30:]
    flash_at = 0x400
    ota_at = (flash_at + fm1pkg_make.FLASH_SIZE + 0xFF) & ~0xFF
    return bytes(logical[flash_at:flash_at + fm1pkg_make.FLASH_SIZE]), bytes(logical[ota_at:]), product


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("songs", nargs="+", help="song JSON files for slots 1.. (PC 0..)")
    ap.add_argument("--keys", default=DEMO_KEYS, help="the fm1-emu key script (default: the demo)")
    ap.add_argument("--snap", default=DEMO_SNAP, help="screens at these ms")
    ap.add_argument("--ms", type=int, help="run this long (default: 500 ms after the last screen)")
    ap.add_argument("--wav", help="write what the emulator played (stereo 44.1 kHz)")
    ap.add_argument("--pedal-channel", type=int, help="the pedal settings: its channel (0 off, 1..16, 17 any)")
    ap.add_argument("--count-in", type=int, help="the pedal settings: bars of count-in (0, 1, 2)")
    ap.add_argument("--emu", type=Path, default=EMU, help=f"the fm1-emu batch runner (default: {EMU})")
    a = ap.parse_args()
    if not a.emu.exists():
        sys.exit(f"no fm1-emu at {a.emu} (cargo build --release -p fm1-emu in the emulator's checkout)")
    if len(a.songs) > 8:
        sys.exit("8 songs at most")
    pkg = ROOT / "build" / "felucca.fwsc"
    if not pkg.exists():
        sys.exit("no build/felucca.fwsc: ./build.sh first")
    flash, ota, product = unpack_fwsc(pkg.read_bytes())
    img = bytearray(flash) + b"\xFF" * (SONG_BASE + 18 * ST_SECTOR - len(flash))   # (to the pedal settings' sectors)
    for k, path in enumerate(a.songs):
        with open(path) as f:
            blob = fm1_song.pack(json.load(f))
        at = SONG_BASE + 2 * k * ST_SECTOR
        img[at:at + ST_SECTOR] = st_object(OBJ_SONG0 + k, blob)
        print(f"slot {k} (PC {k}, key {k + 1}): {blob[4:16].rstrip(bytes(1)).decode()}, {len(blob)} bytes")
    if a.pedal_channel is not None or a.count_in is not None:
        ch = 16 if a.pedal_channel is None else a.pedal_channel
        count = a.count_in or 0
        if not (0 <= ch <= 17 and 0 <= count <= 2):
            sys.exit("the pedal channel is 0..17, the count-in 0..2")
        at = SONG_BASE + 2 * (OBJ_ARRSET - OBJ_SONG0) * ST_SECTOR
        img[at:at + ST_SECTOR] = st_object(OBJ_ARRSET, b"ARS1" + bytes([ch, count]) + bytes(10))
        print(f"pedal channel {ch}, count-in {count}")
    out = ROOT / "build" / "emu"
    (out / "work").mkdir(parents=True, exist_ok=True)
    emu_pkg = out / "felucca-emu.fwsc"
    emu_pkg.write_bytes(fm1pkg_make.ufw(bytes(img), ota, product))
    snaps = [int(x) for x in a.snap.split(",") if x.strip()]
    ms = a.ms or (max(snaps) + 500 if snaps else 5000)
    print(f"running {ms} ms in the emulator (about {ms * 30 // 1000} s here)...")
    cmd = [str(a.emu), "--fwsc", str(emu_pkg), "--ms", str(ms), "--keys", a.keys, "--snap", a.snap]
    if a.wav:                                       # (fm1-emu writes it with its summary, which --quiet leaves out)
        cmd += ["--wav", str(Path(a.wav).resolve())]
    else:
        cmd += ["--quiet"]
    r = subprocess.run(cmd, cwd=out, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout[-2000:], r.stderr[-2000:])
    try:
        from PIL import Image
    except ImportError:
        Image = None
    for t in snaps:
        bmp = out / "work" / f"lcd_{t}.bmp"
        if not bmp.exists():
            print(f"  no screen at {t} ms")
        elif Image:
            Image.open(bmp).save(out / f"lcd_{t}.png")
            print(f"  {out / f'lcd_{t}.png'}")
        else:
            print(f"  {bmp}")
    if a.wav:
        print(f"  {a.wav}")


if __name__ == "__main__":
    main()
