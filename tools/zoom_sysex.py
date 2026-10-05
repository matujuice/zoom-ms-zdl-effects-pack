#!/usr/bin/env python3
"""Talk to an original Zoom MS pedal over USB MIDI from the PC, and log what it sends back.

    py tools/zoom_sysex.py ports                 list MIDI ports
    py tools/zoom_sysex.py hello                 identity request, prints the reply
    py tools/zoom_sysex.py tempo 140             patch tempo SysEx 31 03 08 (40..250)
    py tools/zoom_sysex.py knob 0 2 50           param edit: slot (0-based) param value
    py tools/zoom_sysex.py patch                 request the current patch, hex dump
    py tools/zoom_sysex.py patchdiff             patch dump now and after Enter, changed bytes
    py tools/zoom_sysex.py listen 30             log everything the pedal sends for 30 s
    py tools/zoom_sysex.py raw 52 00 58 33       send any SysEx (F0/F7 added)

Options: --id 58 (MS-50G; the MS-60B on 50G firmware answers to 58), --port NAME (part of
the port name; default: the first port with ZOOM or MS in its name), --wait 1.5 (seconds
to listen after sending), --no-enable (skip the 52 00 id 50 edit enable sent before
edits). Close Zoom Effect Manager first: Windows lets only one program open the port.

Needs mido and python-rtmidi (py -m pip install mido python-rtmidi).

Message formats are from thammer/zoom-explorer and g200kg/zoom-ms-utility (the same
ones the iPhone Mozaic scripts use, PR #13); values are 7-bit LSB then MSB.
"""
from __future__ import annotations

import argparse
import sys
import time

try:
    import mido
except ImportError:
    sys.exit("Needs mido and python-rtmidi:  py -m pip install mido python-rtmidi")


def pick(names: list[str], want: str | None) -> str:
    for n in names:
        if (want and want.lower() in n.lower()) or (not want and ("zoom" in n.lower() or "ms-" in n.lower())):
            return n
    sys.exit(f"No MIDI port matching {want or 'ZOOM/MS'}. Ports: {names or 'none'}")


def hexs(data) -> str:
    return " ".join(f"{b:02X}" for b in data)


def show(msg, t0: float) -> None:
    t = time.monotonic() - t0
    if msg.type == "sysex":
        d = list(msg.data)
        note = ""
        if len(d) >= 6 and d[0] == 0x7E and d[2] == 0x06 and d[3] == 0x02:
            note = f"   identity reply: maker {d[4]:02X} model {d[5]:02X} (58 = MS-50G, 5F = MS-60B)"
        if len(d) >= 4 and d[0] == 0x52 and d[3] == 0x31 and len(d) >= 8:
            note = f"   param: slot {d[4]} param {d[5]} value {d[6] + 128 * d[7]}"
            if d[4] == 3 and d[5] == 8:
                note += "  (patch tempo?)"
        print(f"{t:8.3f}s  IN  F0 {hexs(d)} F7  ({len(d) + 2} bytes){note}")
    elif msg.type != "clock" and msg.type != "active_sensing":
        print(f"{t:8.3f}s  IN  {msg}")


def drain(inp, seconds: float, t0: float, keep: list | None = None) -> None:
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        for msg in inp.iter_pending():
            show(msg, t0)
            if keep is not None and msg.type == "sysex":
                keep.append(list(msg.data))
        time.sleep(0.002)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd")
    ap.add_argument("args", nargs="*")
    ap.add_argument("--id", default="58")
    ap.add_argument("--port")
    ap.add_argument("--wait", type=float, default=1.5)
    ap.add_argument("--no-enable", action="store_true")
    a = ap.parse_args()

    if a.cmd == "ports":
        print("in: ", mido.get_input_names())
        print("out:", mido.get_output_names())
        return

    zid = int(a.id, 16)
    out_name = pick(mido.get_output_names(), a.port)
    in_name = pick(mido.get_input_names(), a.port)
    print(f"out: {out_name}\nin:  {in_name}\nid:  {zid:02X}")
    t0 = time.monotonic()
    with mido.open_output(out_name) as out, mido.open_input(in_name) as inp:
        def send(data: list[int]) -> None:
            print(f"{time.monotonic() - t0:8.3f}s  OUT F0 {hexs(data)} F7")
            out.send(mido.Message("sysex", data=data))

        def enable() -> None:
            if not a.no_enable:
                send([0x52, 0x00, zid, 0x50])
                drain(inp, 0.1, t0)

        if a.cmd == "hello":
            send([0x7E, 0x7F, 0x06, 0x01])
        elif a.cmd == "tempo":
            bpm = int(a.args[0])
            enable()
            send([0x52, 0x00, zid, 0x31, 0x03, 0x08, bpm & 0x7F, bpm >> 7])
        elif a.cmd == "knob":
            slot, param, val = (int(x) for x in a.args[:3])
            enable()
            send([0x52, 0x00, zid, 0x31, slot, param, val & 0x7F, val >> 7])
        elif a.cmd in ("patch", "patchdiff"):
            enable()
            got: list = []
            send([0x52, 0x00, zid, 0x29])
            drain(inp, a.wait, t0, got)
            if a.cmd == "patchdiff":
                first = max(got, key=len) if got else []
                input("Change something on the pedal (tempo), then press Enter... ")
                got = []
                send([0x52, 0x00, zid, 0x29])
                drain(inp, a.wait, t0, got)
                second = max(got, key=len) if got else []
                if not first or not second:
                    sys.exit("No patch dump came back.")
                diff = [(i, x, y) for i, (x, y) in enumerate(zip(first, second)) if x != y]
                print(f"{len(first)} / {len(second)} bytes, {len(diff)} changed:")
                for i, x, y in diff:
                    print(f"  byte {i:4d}: {x:02X} -> {y:02X}")
            a.wait = 0
        elif a.cmd == "listen":
            print("Listening; change things on the pedal. Ctrl+C to stop.")
            a.wait = float(a.args[0]) if a.args else 30.0
        elif a.cmd == "raw":
            send([int(x, 16) for x in a.args])
        else:
            sys.exit(__doc__)
        drain(inp, a.wait, t0)
    print("done")


if __name__ == "__main__":
    main()
