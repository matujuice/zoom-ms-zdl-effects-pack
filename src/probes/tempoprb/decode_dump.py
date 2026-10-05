#!/usr/bin/env python3
"""Decode a recording of TempoPrb in Mode DUMP.

    python3 decode_dump.py recording.wav

Record the pedal output (mono or stereo WAV, 16/24/32-bit PCM, any sample rate) for at
least 8 s per frame, with Mode = DUMP and a moderate Level so nothing clips. Every frame
found is printed as 32 words; from the second frame on, the words that changed are
marked with *. Change the tempo (tap, the menu or the 31 03 08 SysEx) between frames
and look for words that follow it.

Frame layout (see the header of tempoprb.c): 0.3 s silence, 0.2 s 689 Hz start tone,
32 words x 32 bits MSB first, 256 samples (at 44.1 kHz) per bit, 1378 Hz = 0,
2756 Hz = 1. Bits are told apart by counting zero crossings, so level and EQ hardly
matter.
"""
from __future__ import annotations

import bisect
import struct
import sys
import wave

NAMES = (["magic", "slot", "state[0]", "ctx[1]", "state[24]", "Sync raw", "Time raw",
          "x", "y"]
         + [f"tab2 w{k}" for k in range(11)]
         + [f"tab1 w{k}" for k in range(11)]
         + ["blocks"])
MAGIC = 0x54505231
PRE_LEN, BIT_LEN, PRE_HALF = 8820, 256, 32       # samples at 44.1 kHz


def read_wav(path: str) -> tuple[list[float], int]:
    with wave.open(path, "rb") as w:
        ch, width, sr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    step = ch * width
    out = []
    for i in range(0, len(raw) - step + 1, step):
        b = raw[i:i + width]
        if width == 1:
            v = b[0] - 128
        elif width == 2:
            v = struct.unpack("<h", b)[0]
        elif width == 3:
            v = int.from_bytes(b, "little", signed=True)
        else:
            v = struct.unpack("<i", b)[0]
        out.append(float(v))
    return out, sr


def crossings(x: list[float]) -> list[float]:
    """Zero-crossing times (samples, interpolated), with hysteresis against noise."""
    peak = max((abs(v) for v in x), default=0.0)
    thr = 0.1 * peak
    out, sign, last_i = [], 0, 0
    for i, v in enumerate(x):
        s = 1 if v > thr else (-1 if v < -thr else 0)
        if s == 0:
            continue
        if sign and s != sign:
            # crossing somewhere between last_i and i: interpolate on the raw samples
            j = last_i
            while j < i and (x[j] > 0) == (sign > 0):
                j += 1
            a, b = x[j - 1], x[j]
            out.append(j - 1 + (a / (a - b) if a != b else 0.5))
        sign, last_i = s, i
    return out


def frames(x: list[float], sr: int) -> list[list[int]]:
    sc = sr / 44100.0
    zc = crossings(x)
    lo, hi = 24 * sc, 40 * sc                    # preamble half periods are 32 samples
    need = 200                                   # ~275 crossings in a 0.2 s start tone
    found, k = [], 1
    while k < len(zc):
        run = k
        while run < len(zc) and lo <= zc[run] - zc[run - 1] <= hi:
            run += 1
        if run - k >= need:
            start = zc[k - 1] - 16 * sc          # the triangle starts at -1: first crossing at 1/4 period
            bits0 = start + PRE_LEN * sc
            words = []
            for wd in range(32):
                v = 0
                for bit in range(32):
                    b0 = bits0 + (wd * 32 + bit) * BIT_LEN * sc
                    a, e = b0 + 0.1 * BIT_LEN * sc, b0 + 0.9 * BIT_LEN * sc
                    n = _count(zc, a, e)
                    v = (v << 1) | (1 if n >= 19 else 0)
                words.append(v)
            found.append(words)
            k = run + 32 * 32 * 16                # skip past this frame's bits
        else:
            k = run + 1
    return found


def _count(zc: list[float], a: float, e: float) -> int:
    return bisect.bisect_left(zc, e) - bisect.bisect_left(zc, a)


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    x, sr = read_wav(sys.argv[1])
    fs = frames(x, sr)
    if not fs:
        sys.exit("No dump frame found (Mode DUMP? at least 8 s recorded? clipping?)")
    prev = None
    for n, f in enumerate(fs):
        ok = "ok" if f[0] == MAGIC else "BAD MAGIC, bits misread"
        print(f"frame {n + 1} ({ok})")
        for i, v in enumerate(f):
            mark = "*" if prev is not None and prev[i] != v else " "
            fl = struct.unpack("<f", struct.pack("<I", v))[0]
            fs_ = f"{fl:.6g}" if 1e-6 < abs(fl) < 1e9 else "-"
            print(f" {mark} {i:2d} {NAMES[i]:<10} = 0x{v:08X}  {v:>10d}  float {fs_}")
        prev = f


if __name__ == "__main__":
    main()
