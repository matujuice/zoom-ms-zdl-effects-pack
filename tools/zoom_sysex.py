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
    py tools/zoom_sysex.py seq "52 00 58 50" 3 "52 00 58 31 03 08 0C 01"
                                                 one connection: quoted hex = send, number =
                                                 wait that many seconds (logging replies)

Options: --id 58 (MS-50G; the MS-60B on 50G firmware answers to 58), --port NAME (part of
the port name; default: the first port with ZOOM or MS in its name), --wait 1.5 (seconds
to listen after sending), --no-enable (skip the 52 00 id 50 edit enable sent before
edits). Close Zoom Effect Manager first: Windows lets only one program open the port.

On Windows it needs nothing beyond Python: it talks to the Windows MIDI API (winmm)
through ctypes. Elsewhere it uses mido + python-rtmidi if they are installed.

Message formats are from thammer/zoom-explorer and g200kg/zoom-ms-utility (the same
ones the iPhone Mozaic scripts use, PR #13); values are 7-bit LSB then MSB.
"""
from __future__ import annotations

import argparse
import collections
import ctypes
import os
import sys
import time

# ---- MIDI backends: each port pair has send(data), poll() -> [(kind, bytes)], close() ----

if os.name == "nt":
    from ctypes import wintypes

    _w = ctypes.WinDLL("winmm")
    _PTR = ctypes.c_size_t                         # DWORD_PTR / HMIDI*
    MIM_DATA, MIM_LONGDATA, CALLBACK_FUNCTION, MHDR_DONE = 0x3C3, 0x3C4, 0x30000, 1
    NBUF, BUFLEN = 8, 4096

    class MIDIHDR(ctypes.Structure):
        _fields_ = [("lpData", ctypes.c_void_p), ("dwBufferLength", wintypes.DWORD),
                    ("dwBytesRecorded", wintypes.DWORD), ("dwUser", _PTR),
                    ("dwFlags", wintypes.DWORD), ("lpNext", ctypes.c_void_p),
                    ("reserved", _PTR), ("dwOffset", wintypes.DWORD), ("dwReserved", _PTR * 8)]

    class MIDIOUTCAPSW(ctypes.Structure):
        _fields_ = [("wMid", wintypes.WORD), ("wPid", wintypes.WORD), ("vDriverVersion", wintypes.UINT),
                    ("szPname", wintypes.WCHAR * 32), ("wTechnology", wintypes.WORD),
                    ("wVoices", wintypes.WORD), ("wNotes", wintypes.WORD),
                    ("wChannelMask", wintypes.WORD), ("dwSupport", wintypes.DWORD)]

    class MIDIINCAPSW(ctypes.Structure):
        _fields_ = [("wMid", wintypes.WORD), ("wPid", wintypes.WORD), ("vDriverVersion", wintypes.UINT),
                    ("szPname", wintypes.WCHAR * 32), ("dwSupport", wintypes.DWORD)]

    _CB = ctypes.WINFUNCTYPE(None, _PTR, wintypes.UINT, _PTR, _PTR, _PTR)
    _w.midiInOpen.argtypes = [ctypes.POINTER(_PTR), wintypes.UINT, _PTR, _PTR, wintypes.DWORD]
    _w.midiOutOpen.argtypes = [ctypes.POINTER(_PTR), wintypes.UINT, _PTR, _PTR, wintypes.DWORD]
    for f in ("midiInStart", "midiInStop", "midiInReset", "midiInClose", "midiOutClose", "midiOutReset"):
        getattr(_w, f).argtypes = [_PTR]
    for f in ("midiInPrepareHeader", "midiInUnprepareHeader", "midiInAddBuffer",
              "midiOutPrepareHeader", "midiOutUnprepareHeader", "midiOutLongMsg"):
        getattr(_w, f).argtypes = [_PTR, ctypes.POINTER(MIDIHDR), wintypes.UINT]
    _w.midiInGetDevCapsW.argtypes = [_PTR, ctypes.POINTER(MIDIINCAPSW), wintypes.UINT]
    _w.midiOutGetDevCapsW.argtypes = [_PTR, ctypes.POINTER(MIDIOUTCAPSW), wintypes.UINT]

    def _check(r: int, what: str) -> None:
        if r != 0:
            sys.exit(f"{what} failed (MMRESULT {r}); is Zoom Effect Manager still open?")

    def get_input_names() -> list[str]:
        out = []
        for i in range(_w.midiInGetNumDevs()):
            c = MIDIINCAPSW()
            _w.midiInGetDevCapsW(i, ctypes.byref(c), ctypes.sizeof(c))
            out.append(c.szPname)
        return out

    def get_output_names() -> list[str]:
        out = []
        for i in range(_w.midiOutGetNumDevs()):
            c = MIDIOUTCAPSW()
            _w.midiOutGetDevCapsW(i, ctypes.byref(c), ctypes.sizeof(c))
            out.append(c.szPname)
        return out

    class Ports:
        def __init__(self, out_name: str, in_name: str):
            self.got: collections.deque = collections.deque()    # appended by the driver thread
            self.done: collections.deque = collections.deque()
            self.pending: list = []
            self.hout, self.hin = _PTR(), _PTR()
            _check(_w.midiOutOpen(ctypes.byref(self.hout), get_output_names().index(out_name), 0, 0, 0),
                   "midiOutOpen")
            self._cb = _CB(self._on_in)            # keep a reference while the port is open
            cbp = ctypes.cast(self._cb, ctypes.c_void_p).value
            _check(_w.midiInOpen(ctypes.byref(self.hin), get_input_names().index(in_name), cbp, 0,
                                 CALLBACK_FUNCTION), "midiInOpen")
            self.bufs = [ctypes.create_string_buffer(BUFLEN) for _ in range(NBUF)]
            self.hdrs = [MIDIHDR() for _ in range(NBUF)]
            for k, (b, h) in enumerate(zip(self.bufs, self.hdrs)):
                h.lpData, h.dwBufferLength, h.dwUser = ctypes.cast(b, ctypes.c_void_p), BUFLEN, k
                _w.midiInPrepareHeader(self.hin.value, ctypes.byref(h), ctypes.sizeof(h))
                _w.midiInAddBuffer(self.hin.value, ctypes.byref(h), ctypes.sizeof(h))
            self.closing = False
            _check(_w.midiInStart(self.hin.value), "midiInStart")

        def _on_in(self, h, msg, inst, p1, p2):     # driver thread: copy and queue only
            if msg == MIM_DATA:
                self.got.append(("short", [p1 & 0xFF, (p1 >> 8) & 0xFF, (p1 >> 16) & 0xFF]))
            elif msg == MIM_LONGDATA:
                hdr = MIDIHDR.from_address(p1)
                if hdr.dwBytesRecorded:
                    self.got.append(("sysex", list(ctypes.string_at(hdr.lpData, hdr.dwBytesRecorded))))
                self.done.append(hdr.dwUser)

        def poll(self) -> list:
            while self.done and not self.closing:  # give finished buffers back to the driver
                h = self.hdrs[self.done.popleft()]
                _w.midiInAddBuffer(self.hin.value, ctypes.byref(h), ctypes.sizeof(h))
            res = []
            while self.got:
                kind, d = self.got.popleft()
                if kind == "sysex":                # a long SysEx can arrive in several buffers
                    self.pending += d
                    if self.pending[-1] == 0xF7:
                        res.append(("sysex", self.pending))
                        self.pending = []
                else:
                    res.append((kind, d))
            return res

        def send(self, data: list[int]) -> None:
            buf = ctypes.create_string_buffer(bytes([0xF0] + data + [0xF7]))
            h = MIDIHDR()
            h.lpData, h.dwBufferLength = ctypes.cast(buf, ctypes.c_void_p), len(data) + 2
            _w.midiOutPrepareHeader(self.hout.value, ctypes.byref(h), ctypes.sizeof(h))
            _check(_w.midiOutLongMsg(self.hout.value, ctypes.byref(h), ctypes.sizeof(h)), "midiOutLongMsg")
            end = time.monotonic() + 2.0
            while not (h.dwFlags & MHDR_DONE) and time.monotonic() < end:
                time.sleep(0.001)
            _w.midiOutUnprepareHeader(self.hout.value, ctypes.byref(h), ctypes.sizeof(h))

        def close(self) -> None:
            self.closing = True
            _w.midiInStop(self.hin.value)
            _w.midiInReset(self.hin.value)
            time.sleep(0.05)
            for h in self.hdrs:
                _w.midiInUnprepareHeader(self.hin.value, ctypes.byref(h), ctypes.sizeof(h))
            _w.midiInClose(self.hin.value)
            _w.midiOutReset(self.hout.value)
            _w.midiOutClose(self.hout.value)
else:
    try:
        import mido
    except ImportError:
        sys.exit("Off Windows this needs mido and python-rtmidi.")

    def get_input_names() -> list[str]:
        return mido.get_input_names()

    def get_output_names() -> list[str]:
        return mido.get_output_names()

    class Ports:
        def __init__(self, out_name: str, in_name: str):
            self.out, self.inp = mido.open_output(out_name), mido.open_input(in_name)

        def poll(self) -> list:
            return [("sysex", [0xF0] + list(m.data) + [0xF7]) if m.type == "sysex" else ("short", m.bytes())
                    for m in self.inp.iter_pending()]

        def send(self, data: list[int]) -> None:
            self.out.send(mido.Message("sysex", data=data))

        def close(self) -> None:
            self.out.close(); self.inp.close()


def pick(names: list[str], want: str | None) -> str:
    for n in names:
        if (want and want.lower() in n.lower()) or (not want and ("zoom" in n.lower() or "ms-" in n.lower())):
            return n
    sys.exit(f"No MIDI port matching {want or 'ZOOM/MS'}. Ports: {names or 'none'}")


def hexs(data) -> str:
    return " ".join(f"{b:02X}" for b in data)


def show(kind: str, raw: list[int], t0: float) -> None:
    t = time.monotonic() - t0
    if kind == "sysex":
        d = raw[1:-1] if raw and raw[0] == 0xF0 else raw   # without F0 / F7
        note = ""
        if len(d) >= 6 and d[0] == 0x7E and d[2] == 0x06 and d[3] == 0x02:
            note = f"   identity reply: maker {d[4]:02X} model {d[5]:02X} (58 = MS-50G, 5F = MS-60B)"
        if len(d) >= 4 and d[0] == 0x52 and d[3] == 0x31 and len(d) >= 8:
            note = f"   param: slot {d[4]} param {d[5]} value {d[6] + 128 * d[7]}"
            if d[4] == 3 and d[5] == 8:
                note += "  (patch tempo?)"
        print(f"{t:8.3f}s  IN  F0 {hexs(d)} F7  ({len(d) + 2} bytes){note}")
    elif raw and raw[0] not in (0xF8, 0xFE):              # skip clock, active sensing
        print(f"{t:8.3f}s  IN  {hexs(raw)}")


def drain(port, seconds: float, t0: float, keep: list | None = None) -> None:
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        for kind, raw in port.poll():
            show(kind, raw, t0)
            if keep is not None and kind == "sysex":
                keep.append(raw[1:-1])
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
        print("in: ", get_input_names())
        print("out:", get_output_names())
        return

    zid = int(a.id, 16)
    out_name = pick(get_output_names(), a.port)
    in_name = pick(get_input_names(), a.port)
    print(f"out: {out_name}\nin:  {in_name}\nid:  {zid:02X}")
    t0 = time.monotonic()
    inp = Ports(out_name, in_name)
    try:
        def send(data: list[int]) -> None:
            print(f"{time.monotonic() - t0:8.3f}s  OUT F0 {hexs(data)} F7")
            inp.send(data)

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
        elif a.cmd == "seq":
            for step in a.args:
                if " " in step.strip() or len(step.strip()) == 2 and not step.strip().isdigit():
                    send([int(x, 16) for x in step.split()])
                    drain(inp, 0.05, t0)
                else:
                    drain(inp, float(step), t0)
        else:
            sys.exit(__doc__)
        drain(inp, a.wait, t0)
    except KeyboardInterrupt:
        pass
    finally:
        inp.close()
    print("done")


if __name__ == "__main__":
    main()
