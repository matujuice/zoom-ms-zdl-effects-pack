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
