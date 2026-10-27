#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read BackBeat's battery log over the USB serial console (firmware/src/battlog.c, console.c `batt`).

    tools/fm1_batt.py [--port /dev/ttyACM0] [--csv FILE] [--clear]

The firmware keeps one reading a minute of the battery divider (the SARADC's raw 10 bits on PB1, smoothed) in RAM
that survives resets, with what the minute was like (playing, played, the screen dark, a USB host alive, the audio
gate closed, the first minute after a boot). Run the unit on its battery in the state to measure (stopped with the
screen on, stopped with it dark, playing a song), plug it in, read the log here: the readings, and the slope of
each run of minutes in one state (raw counts an hour: the number to compare between firmwares and states; the
counts to volts are not calibrated). The console must be on: SETUP > USB SERIAL = ON. Needs pyserial.
A reading taken with the cable in is charging, not discharging: the host flag (8) marks a host that enumerated the
unit, a charger alone leaves no mark, so note when the cable went in and out."""
import argparse, sys, time
try:
    import serial, serial.tools.list_ports
except ImportError:
    sys.exit("fm1_batt.py: needs pyserial (pip install pyserial)")

FLAGS = [(1, "playing"), (2, "played"), (4, "dark"), (8, "host"), (16, "gated"), (32, "boot")]


def find_port():
    for p in serial.tools.list_ports.comports():
        if (p.vid, p.pid) == (0x1209, 0x0001) or "Felucca" in (p.description or "") or "BackBeat" in (p.description or ""):
            return p.device
    return None


def console(port, cmd, end=b"end\r\n", timeout=20.0):
    with serial.Serial(port, 115200, timeout=0.2) as s:
        s.dtr = True
        time.sleep(0.2)
        s.reset_input_buffer()
        s.write(b"\r")
        time.sleep(0.2)
        s.reset_input_buffer()
        s.write(cmd.encode() + b"\r")
        buf = b""
        t0 = time.time()
        while time.time() - t0 < timeout:
            chunk = s.read(4096)
            if chunk:
                buf += chunk
                if end in buf:
                    break
        return buf.decode(errors="replace")


def state_name(f):
    f &= ~32                                     # (boot: a mark, not a state)
    names = [n for b, n in FLAGS if f & b and n != "played"]
    if f & 2 and not f & 1:
        names.append("played")
    return "+".join(names) if names else "stopped"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", help="the console's serial port (default: the first FM-1 found)")
    ap.add_argument("--csv", help="write the readings as CSV (minute, raw, flags, state)")
    ap.add_argument("--clear", action="store_true", help="empty the log on the unit instead of reading it")
    a = ap.parse_args()
    port = a.port or find_port()
    if not port:
        sys.exit("fm1_batt.py: no FM-1 console found (SETUP > USB SERIAL = ON; or --port)")
    if a.clear:
        print(console(port, "batt clear", end=b"> ").strip())
        return
    text = console(port, "batt")
    rows = []
    for line in text.splitlines():
        w = line.split()
        if len(w) == 3 and all(x.isdigit() for x in w):
            rows.append(tuple(int(x) for x in w))
    if not rows:
        sys.exit("fm1_batt.py: no readings (the log is empty, or the console did not answer):\n" + text)
    if a.csv:
        with open(a.csv, "w") as f:
            f.write("minute,raw,flags,state\n")
            for m, v, fl in rows:
                f.write(f"{m},{v},{fl},{state_name(fl)}\n")
        print(f"{len(rows)} readings -> {a.csv}")
    print(f"{len(rows)} readings, one a minute (minute {rows[0][0]} .. {rows[-1][0]})")
    print("minute  raw  state")
    for m, v, fl in rows:
        print(f"{m:6d} {v:4d}  {state_name(fl)}{'  (boot)' if fl & 32 else ''}")
    # runs: consecutive minutes in one state (the boot mark ignored), their slope by least squares
    print("\nruns (10 minutes or more): slope in raw counts an hour, negative = discharging")
    runs, cur = [], []
    for r in rows:
        if cur and ((r[2] & ~32) != (cur[-1][2] & ~32) or r[0] != cur[-1][0] + 1):
            runs.append(cur)
            cur = []
        cur.append(r)
    if cur:
        runs.append(cur)
    for run in runs:
        if len(run) < 10:
            continue
        n = len(run)
        xs = [r[0] for r in run]
        ys = [r[1] for r in run]
        mx, my = sum(xs) / n, sum(ys) / n
        sxx = sum((x - mx) ** 2 for x in xs)
        slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx if sxx else 0.0
        print(f"  {state_name(run[0][2]):28s} minutes {xs[0]:5d}..{xs[-1]:5d} ({n:4d})  raw {ys[0]:4d} -> {ys[-1]:4d}"
              f"  slope {slope * 60:+8.1f} /h")


if __name__ == "__main__":
    main()
