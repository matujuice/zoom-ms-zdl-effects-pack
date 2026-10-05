# TempoPrb: can a custom effect see the pedal's tempo?

A hardware probe, not a release effect. It runs the call chain the stock TAPEECH3 delay
uses to follow the patch tempo (docs/TEMPO-SYNC.md sections 4 and 8) and makes the
results audible. The full description is the header of `tempoprb.c`.

Build on the PC: `py build_all.py tempoprb` -> `dist\TempoPrb.ZDL`. Do not put it in a
release.

## Before you load it

It may crash the pedal on load or on the first knob turn. It reads firmware memory, its
Sync knob carries the descriptor flag 0x28 that no custom effect has used yet, and with
Call ON it calls a firmware function. If the pedal freezes, switch it off and on, remove
the effect with Zoom Effect Manager, and report at which step it froze: that is a
result too. Use a patch with nothing else important in it.

## Test, in this order

Put TempoPrb alone in a patch, feed it nothing (or a quiet sound), Level about 50.

1. **Load.** Does it load, does the screen show it, does the pedal still respond?
2. **Calibration** (Mode BLIP, Watch SYNC, Call OFF): turn Sync (page 2). Each step
   must beep. A warble once a second means the probe could not find its own data;
   then go straight to step 5.
3. **Does anything follow the tempo?** (Mode BLIP, Watch TABLE, Call OFF, Sync S4):
   change the patch tempo with the tempo menu or tap tempo, then send the 31 03 08
   SysEx from the iPhone (it showed no effect in the iPhone test, 2026-10-05; a beep
   here would mean it does reach the pedal). Beeps when the tempo changes = some value
   the probe can read follows it.
   High beep = second table, low beep = first table. Constant beeping = something
   changes all the time (note that too). Repeat with Watch ROW and Watch X.
4. **The TAPEECH3 recipe** (Mode CLICK, Watch RCPE, Sync S4, then Call ON last): clicks
   = the pedal handed back a delay time; does the click speed follow the tempo when you
   change it? A low hum = the value is not a usable delay. If it freezes on Call ON,
   say so.
5. **Data dump** (Mode DUMP): record the output for 30 s (phone or interface, no
   clipping), change the tempo once in the middle, and send the file. It is decoded
   with `python3 src/probes/tempoprb/decode_dump.py recording.wav`, which prints every
   value the probe reads and marks the ones that changed. Do it once with Call OFF and
   once with Call ON.

## What the host test covers

`python3 tests/run.py tempoprb` checks the probe's own logic against fake host memory:
beeps only on changes, click timing, the state[24] call only with Call ON and a
firmware-looking pointer, no reads outside the two tables, and a dump that decodes
from a 44.1 kHz file and a noisy 48 kHz one. It cannot say what the pedal keeps at
those addresses; only the pedal can.
