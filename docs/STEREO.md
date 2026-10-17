# Stereo input: plan for the pack effects

Decided with Luca, 2026-10-16. Not built yet.

## Why

MIDISTOMP v1.2 runs the MS-70CDR firmware on every pedal, and the MS-70CDR has
real stereo input. Every pack effect except the Looper is mono today:

- it processes the **left half** of the chain buffer in place (`fxBuf[0..7]`);
- then it copies the result to the right (`fxBuf[i + 8] = fxBuf[i]`).

So **the right input is thrown away**. On the MS-50G and MS-60B that changes
nothing, because there the firmware copies L into R. On the MS-70CDR it does:

- **A stereo source** loses whatever is only on its right side.
- **Only the R jack plugged:** the firmware switches to stereo with a silent L,
  so every pack effect goes **silent**, while stock effects and other people's
  ZDLs still play. This happened to Luca on 2026-10-16. DubSiren still made its
  own sound but passed no input.

## What we do

1. **Effects that can go stereo get ported to stereo** (Luca). They read and
   write both halves.
2. **The rest get a mono-sum input:** read `(L + R) / 2` instead of L, process,
   then copy to both sides as today. This is a one-line change per effect.
   - It needs no setting: on the MS-50G and MS-60B R is already a copy of L,
     and on the MS-70CDR with only L plugged the firmware copies L into R. In
     both cases (L + R) / 2 = L.
   - It costs one add and one multiply per sample.

## What porting costs

Measured on Luca's MS-70CDR with the M0 probe firmware (zoom-ms-midistomp
`docs/mixer-mode-m0.md`), 2026-10-16:

- **CPU.** True stereo (two independent channels) about doubles the cost. At
  the busiest, a block has about 54,000 cycles available. Stock effects use
  roughly 70-120 cycles per DSP cost unit. **Choral** already uses about
  15,500 cycles per block mono.
- **Memory.** A stereo delay or buffer holds half the time in the same arena
  (about 705 KB per effect). Scrub's 7.9 s freeze would become about 3.9 s.
- **Declared DSP cost.** Raise each effect's cost to match what it really uses,
  measured with the probe. Choral declares 20, but measures like about 140.

## Measured cost (M0 probe, MS-70CDR, Luca's settings, 2026-10-16)

Cycles per 8-sample block, all effects on, playing. Every pack effect declares
a DSP cost of 20 today. Stock effects measure about 100 cycles per cost unit,
so the real cost is about the average divided by 100.

| Effect | Average | Max | Real cost |
|---|---|---|---|
| WaveFold | 1,399 | 4,009 | ~14 |
| EuGate | 2,284 | 4,845 | ~23 |
| Sweep | 2,459 | 6,732 | ~25 |
| GridDly | 2,643 | 5,081 | ~26 |
| Scrub | 2,765 | 5,589 | ~28 |
| Breather | 2,863 | 6,353 | ~29 |
| DualShft | 3,672 | 8,148 | ~37 |
| DubSiren | 3,807 | 6,958 | ~38 |
| S.GN_L | 4,705 | 10,050 | ~47 |
| DirtBox | 5,421 | 10,643 | ~54 |
| **Choral** | **24,100** | **29,702** | **~240** (15,494 in an earlier patch) |

A block has about 54,000 cycles at most. Choral alone can take more than half
of that, and its real cost exceeds the whole stock DSP limit (225). The
numbers depend on settings (engine, model, voices); these are one setting
each.

**Declared costs set 2026-10-16** (`dsp_cost` in each `manifest_pedal.json`, written to
the descriptor by `build/linker.py`): average / 100 rounded up plus about 25 % for heavier
settings. WaveFold 18, EuGate 29, Sweep 32, GridDly 33, Scrub 35, Breather 36,
DualShft 46, DubSiren 48, S.GN_L 59, DirtBox 68. **Choral 150 for now**: its honest 240 is
over the firmware's 225 limit and it could not load at all; it gets optimized, then
re-measured. The Looper is not measured yet and keeps 20.

## Re-measured after the CPU cuts (M0 probe v2, MS-60B, 2026-10-17)

One effect per patch (the other five slots GAIN), default knobs, P3 CYC after 10 s of
playing. Net = AVG minus the 228 cycles an empty GAIN slot reads (probe overhead). MAX is not
used: empty slots already peak at 1,800-2,500 from interrupts. Calibrated on two stock
effects: Hall (27.4) 2,408 net = 88 cycles per unit, Delay (15.6) 953 net = 61; 75 used.
Declared cost = net / 75 + 25 % for heavier settings, rounded up.

| Effect | AVG | Net | Net / 75 | Declared |
|---|---|---|---|---|
| Choral | 7,532 | 7,304 | 97.4 | 122 |
| DualShft | 2,832 | 2,604 | 34.7 | 44 |
| DirtBox | 2,486 | 2,258 | 30.1 | 38 |
| Looper | 2,122 | 1,894 | 25.3 | 32 |
| GridDly | 2,082 | 1,854 | 24.7 | 31 |
| WaveFold | 1,729 | 1,501 | 20.0 | 26 |
| DubSiren | 1,720 | 1,492 | 19.9 | 25 |
| S.GN_L | 1,304 | 1,076 | 14.3 | 18 |
| Breather | 1,278 | 1,050 | 14.0 | 18 |
| Sweep | 1,193 | 965 | 12.9 | 17 |
| Scrub | 1,013 | 785 | 10.5 | 14 |
| EuGate | 516 | 288 | 3.8 | 5 |

These replace the 2026-10-16 costs above. Default knobs only: DirtBox's model, Choral's size and
chord, the Looper overdubbing can cost more.

## Per effect: first pass (for Luca to confirm)

| Effect | Today | Suggested | Notes |
|---|---|---|---|
| GridDly | mono | **stereo** | delay: 2x memory, or half the max time |
| DualShft | mono | **stereo** | check CPU |
| Breather | mono | **stereo** | reverb: a second comb/allpass set, about 2x CPU |
| Sweep | mono | **stereo** | filter: cheap to double |
| DirtBox | mono | **stereo** | distortion: cheap, two independent channels |
| WaveFold | mono | **stereo** | cheap, two independent channels |
| EuGate | mono | **stereo** | gate: one shared pattern, L and R gated together |
| Scrub | mono | stereo? | halves the freeze time (7.9 s to about 3.9 s) |
| DubSiren | mono | stereo? | the siren stays mono; the input and delay could be stereo |
| S.GN_L | mono | mono-sum? | |
| Choral | mono | **mono-sum** | already the heaviest effect; stereo would about double 15,500 cycles |
| Looper | stereo | stereo | already true stereo (mono only when it doesn't fit) |

Each port keeps the pack rules (docs/SAFE-DSP-RULES.md). Each needs a host
test showing L and R stay apart, and a declared cost updated from a probe
measurement.
