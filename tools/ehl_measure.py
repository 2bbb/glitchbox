#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Measure the audio ISR's load on a real FM-1 running an EHL_MEASURE=1 build.

Talks to the firmware's USB serial console (status, ins, set, fxl, maxreset) and plays notes over USB-MIDI on
channels 1..4 (MIDI IN routing CH1-4: one channel a track), re-striking them so the voices stay busy. For each
scenario: the INSERTs are set, the peaks reset, the notes play for --seconds, then `status` is read.

    .venv/bin/python tools/ehl_measure.py                 # every scenario, a table on stdout
    .venv/bin/python tools/ehl_measure.py --json out.json # and the raw numbers
"""
import argparse
import glob
import json
import os
import re
import select
import sys
import termios
import time

import mido

IT = {"OFF": 0, "MUFF": 9, "BASS": 10, "SMASH": 11, "ADPCM": 12, "OVRFL": 13, "MISRD": 14, "SHEAR": 15, "UNDRN": 16}
PF_DAMAGE = (1 << 18) | (1 << 19) | (1 << 20) | (1 << 21)   # WRAP BITFLIP UNDERRUN ADPCM (perform.c PF_*)
HALF_US = 128 * 1000000 / 44100                              # the audio half the ISR renders: 2902 us


class Console:
    def __init__(self, path):
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(self.fd)
        attrs[0] = attrs[1] = attrs[3] = 0                   # raw: no input/output/local processing
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        self.cmd("")

    def read_until_prompt(self, timeout=2.0):
        out, end = b"", time.time() + timeout
        while time.time() < end and not out.endswith(b"> "):
            r, _, _ = select.select([self.fd], [], [], 0.05)
            if r:
                try:
                    out += os.read(self.fd, 4096)
                except BlockingIOError:
                    pass
        return out.decode("ascii", "replace")

    def cmd(self, line):
        os.write(self.fd, (line + "\r").encode())
        return self.read_until_prompt()

    def status(self):
        text = self.cmd("status")
        return {k: int(v) for k, v in re.findall(r"^(\w+) (-?\d+)\r?$", text, re.M)}


def find_port(names, patterns):
    for n in names:
        if any(p.lower() in n.lower() for p in patterns):
            return n
    raise SystemExit(f"no MIDI port like {patterns} in {names}")


def play(out, seconds, notes_per_track, drums):
    """re-strike chords on channels 0..2 every 0.5 s and a drum lane pattern on channel 3 every 1/8"""
    chords = [[48, 55, 60, 63, 67, 70, 72, 75], [36, 43, 48, 51], [60, 63, 67, 70, 74, 77, 79, 82]]
    end, t, step = time.time() + seconds, 0, 0
    while time.time() < end:
        if step % 4 == 0:
            for ch in range(3):
                for n in chords[ch][:notes_per_track]:
                    out.send(mido.Message("note_off", channel=ch, note=n))
                    out.send(mido.Message("note_on", channel=ch, note=n, velocity=110))
        if drums:
            for n in (36, 38, 42) if step % 2 == 0 else (42, 46):
                out.send(mido.Message("note_on", channel=3, note=n, velocity=120))
        step += 1
        time.sleep(0.125)


def notes_off(out):
    for ch in range(4):
        out.send(mido.Message("control_change", channel=ch, control=123, value=0))   # all notes off


SCENARIOS = [
    # (name, per-track INSERT TYPEs T1..T4, FX latched mask, notes a synth track)
    ("idle, power-on parts", None, 0, 0),
    ("power-on parts playing", None, 0, 8),
    ("all INSERTs OFF playing", ["OFF"] * 4, 0, 8),
    ("ADPCM x4", ["ADPCM"] * 4, 0, 8),
    ("OVRFL x4", ["OVRFL"] * 4, 0, 8),
    ("MISRD x4", ["MISRD"] * 4, 0, 8),
    ("SHEAR x4", ["SHEAR"] * 4, 0, 8),
    ("UNDRN x4", ["UNDRN"] * 4, 0, 8),
    ("BASS x4 (heaviest stock)", ["BASS"] * 4, 0, 8),
    ("mixed EHL + 4 damage FX", ["ADPCM", "OVRFL", "MISRD", "UNDRN"], PF_DAMAGE, 8),
]


FX_SCENARIOS = [
    ("mixed EHL, no FX", ["ADPCM", "OVRFL", "MISRD", "UNDRN"], 0, 8),
    ("mixed EHL + GATE (stock)", ["ADPCM", "OVRFL", "MISRD", "UNDRN"], 1 << 15, 8),
    ("mixed EHL + REPEAT 1/16 (stock)", ["ADPCM", "OVRFL", "MISRD", "UNDRN"], 1 << 1, 8),
    ("mixed EHL + WRAP", ["ADPCM", "OVRFL", "MISRD", "UNDRN"], 1 << 18, 8),
    ("mixed EHL + BITFLIP", ["ADPCM", "OVRFL", "MISRD", "UNDRN"], 1 << 19, 8),
    ("mixed EHL + UNDERRUN", ["ADPCM", "OVRFL", "MISRD", "UNDRN"], 1 << 20, 8),
    ("mixed EHL + ADPCM FX", ["ADPCM", "OVRFL", "MISRD", "UNDRN"], 1 << 21, 8),
    ("INSERTs OFF + 4 damage FX", ["OFF"] * 4, PF_DAMAGE, 8),
    ("mixed EHL + 4 damage FX", ["ADPCM", "OVRFL", "MISRD", "UNDRN"], PF_DAMAGE, 8),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--serial", help="the console's device (default: the first /dev/cu.usbmodem*)")
    ap.add_argument("--seconds", type=float, default=8.0)
    ap.add_argument("--json")
    ap.add_argument("--set", choices=["inserts", "fx"], default="inserts", help="the scenario table")
    a = ap.parse_args()
    dev = a.serial or (sorted(glob.glob("/dev/cu.usbmodem*")) or [None])[0]
    if not dev:
        raise SystemExit("no /dev/cu.usbmodem*: is the console on (MENU > USB SERIAL)?")
    con = Console(dev)
    banner = con.cmd("status")
    if "ehl" not in banner:
        print(banner)
        raise SystemExit("the console is not an EHL build")
    if "? (help)" in con.cmd("maxreset"):
        raise SystemExit("not an EHL_MEASURE=1 build (no maxreset)")
    out = mido.open_output(find_port(mido.get_output_names(), ["FM-1", "Felucca", "Glitchbox", "FM1"]))
    # each track's INSERT as it is: put back at the end (the firmware autosaves the music and restores it at power-on,
    # so a measurement must not leave its settings behind)
    saved = re.findall(r"^ins (\d) (-?\d+) (-?\d+)\r?$", con.cmd("ins 0"), re.M)
    if len(saved) != 4:
        raise SystemExit("cannot read the INSERTs back (an older EHL_MEASURE build?): not measuring")
    rows = []
    try:
        run(con, out, a, rows)
    finally:
        notes_off(out)
        con.cmd("fxl 0")
        for k, ty, mix in saved:
            con.cmd(f"ins {k} {ty} {mix}")
        print("restored the INSERTs: " + ", ".join(f"T{k} {ty}/{mix}" for k, ty, mix in saved))
    if a.json:
        with open(a.json, "w") as f:
            json.dump(rows, f, indent=1)


def run(con, out, a, rows):
    for name, types, fx, notes in (FX_SCENARIOS if a.set == "fx" else SCENARIOS):
        if types:
            for k, ty in enumerate(types):
                con.cmd(f"ins {k + 1} {IT[ty]}")
        con.cmd(f"fxl {fx}")
        time.sleep(0.3)
        con.cmd("maxreset")
        if notes:
            play(out, a.seconds, notes, True)
        else:
            time.sleep(a.seconds)
        s = con.status()                                     # (read while the notes still sound)
        notes_off(out)
        s["scenario"] = name
        s["max_pct"] = round(100.0 * s.get("audio_max_us", 0) / HALF_US, 1)
        rows.append(s)
        print(f"{name:28s} cpu {s.get('cpu_pct', -1):3d} %  peak {s.get('audio_max_us', -1):5d} us "
              f"({s['max_pct']:5.1f} %)  late {s.get('audio_late', -1):3d}  shed {s.get('voices_shed', -1):3d}  "
              f"given up {s.get('voices_given_up', -1):3d}  cpu2 {s.get('cpu2', '-')} "
              f"({s.get('cpu2_blocks', 0)} blocks, wait max {s.get('cpu2_wait_max_us', 0)} us)", flush=True)
        time.sleep(1.0)


if __name__ == "__main__":
    sys.exit(main())
