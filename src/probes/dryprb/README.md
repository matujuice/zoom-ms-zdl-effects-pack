# DryPrb: can one effect leave a marker for later effects?

A hardware probe, not a release effect. The idea (the Div0 trick, see `docs/TEMPO-SYNC.md`):
the Mozaic bar sync can only edit slots 1 to 3 (tested on the MS-60B, 2026-10-06; slots 4 to
6 ignore it). If an effect in slots 1..3 can write a marker into the Dry buffer and effects
after it can read it, the synced effect could pass the bar on to slots 4..6. This probe
answers the yes/no questions only. The full description is the header of `dryprb.c`.

Build on the PC: `py build_all.py dryprb` -> `dist\DryPrb.ZDL`. Do not put it in a release.

Load it twice in one patch. Role SEND writes the marker, Role READ plays a tone when it
finds it (steady high tone = found; a short low blip once a second = not found). The input
passes through unchanged. Set SEND and READ to the same Where and Shape.

## Test, in this order (Level about 50, a quiet input)

1. **Side by side.** Slot 1 SEND (Where DRYR, Shape DC, Amp 50), slot 2 READ, same Where.
   High tone = the Dry right half reaches the next slot. Blip only = it does not.
2. **Leak check.** Silence the input, slot 1 SEND alone, Amp 100. Do you hear or see
   anything at the output (mixer meter, headphones)? Repeat with Shape ALT. Any sound
   = the marker reaches the output.
3. **Stock effect between.** SEND in slot 1, a stock effect (delay, reverb, comp) in slot 2,
   READ in slot 3. Still found?
4. **Across the edit limit.** SEND in slot 1, READ in slot 5 (a stock effect in 2..4).
5. **Where = DRYL and FXR.** Same as step 1. FXR is the control: it is the buffer the
   audio uses, so expect it to leak or get overwritten; DRYL is the other Dry half.
6. **Shape PULSE.** The tone should switch on and off about every 0.74 s.

Report for each step: found / not found, and for step 2 whether anything is audible.

## Status

Run on the MS-60B on 2026-10-06: the value written in slot 1 was found in slots 2, 3 and 5,
also past a stock filter, and nothing of it reached the output. That result is the basis of
the bar tag (`src/custom/common/drytag.h`, docs/TEMPO-SYNC.md section 10). The host test
(`tests/dryprb_probe.c`) checks the logic. Probe ZDLs go in the loader folder only for a test
round; remove them afterwards.
