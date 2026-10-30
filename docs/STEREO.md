# Stereo input: plan for the pack effects

First plan with Luca 2026-10-16; per-effect plan agreed 2026-10-23 (see "Agreed plan"
at the end, it replaces "What we do" and the first pass). Built 2026-10-28/29: see "Done" at the end.

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
| GRDELAY | 2,643 | 5,081 | ~26 |
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
settings. WaveFold 18, EuGate 29, Sweep 32, GRDELAY 33, Scrub 35, Breather 36,
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
| GRDELAY | 2,082 | 1,854 | 24.7 | 31 |
| WaveFold | 1,729 | 1,501 | 20.0 | 26 |
| DubSiren | 1,720 | 1,492 | 19.9 | 25 |
| S.GN_L | 1,304 | 1,076 | 14.3 | 18 |
| Breather | 1,278 | 1,050 | 14.0 | 18 |
| Sweep | 1,193 | 965 | 12.9 | 17 |
| Scrub | 1,013 | 785 | 10.5 | 14 |
| EuGate | 516 | 288 | 3.8 | 5 |

These replace the 2026-10-16 costs above. Default knobs only: DirtBox's model, Choral's size
and chord can cost more.

## Per effect: first pass (superseded by "Agreed plan" below)

| Effect | Today | Suggested | Notes |
|---|---|---|---|
| GRDELAY | mono | **stereo** | delay: 2x memory, or half the max time |
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

## Agreed plan (Luca, 2026-10-23)

Rule: only effects that add width, or that one would run on a whole mix, are true
stereo, plus any effect whose stereo cost stays under about 40. The rest keep a mono
effect sound but let the stereo signal through. For a stereo synth into a mono effect,
use the firmware's Split mode with two copies of it.

**True stereo.** Both halves of `fxBuf` processed. Costs are estimates (mono net x
about 1.8: knob maths runs once, the sample loop doubles), to be re-measured on the pedal.

| Effect | How | New knob | Declared cost (now -> stereo) |
|---|---|---|---|
| EuGate | one gate pattern, L and R gated together | none | 5 -> 6 |
| Sweep | always stereo | none | 17 -> ~30 |
| Breather | always stereo (second comb/allpass set) | none | 18 -> ~32 |
| S.GN_L | always stereo; packet loss shared, so both sides drop together | none | 18 -> ~32 |
| Scrub | knob picks the buffer layout | **Stereo**: MONO (7.9 s freeze) / ST (3.9 s) | 14 -> ~25 |
| GRDELAY | knob picks the mode | **Stereo**: MONO (7.9 s) / ST (true stereo, 3.9 s) / PING (ping-pong from one buffer, 3.9 s) | 31 -> **44** (mid-way, see below) |
| Choral | pitch tracking stays mono on (L + R) / 2; the voices are spread | **Width** 0-100 | 122 -> ~135 |
| Looper | already true stereo | none | 32 |

**Stereo dry, mono effect sound.** The effect reads `(L + R) / 2`; the dry signal keeps
L and R apart: `out L = dry L * dryG + wet * wetG`, `out R = dry R * dryG + wet * wetG`.
A few adds per block, so the declared cost does not change.

| Effect | Notes |
|---|---|
| DubSiren | the input passes through in stereo (it is never processed); the siren and its echo stay mono, placed by a new **Pan** knob (knob 9, equal power, default centre). Cost stays 25 |
| WaveFold | DJ-style Mix: full stereo up to Mix 50, fades to mono as Mix goes to 100 |
| DirtBox | as WaveFold (all 9 knobs used, so no Pan) |
| DualShft | as WaveFold |

**New knobs.** Each new knob goes in the first free slot (Scrub 8, GRDELAY 9,
Choral 9, DubSiren 9). Defaults: GRDELAY **ST**, Scrub MONO, DubSiren Pan centre.
Old patches don't need to sound the same (Luca, 2026-10-23).

**Declared cost vs mode.** The firmware's DSP Full check sums the fixed declared cost
and never sees the knobs, so MONO saves real CPU but not declared cost. The stereo cost
is declared, except GRDELAY: declared mid-way (44) because its full stereo (~56) is the
only big jump. Check ST and PING on a full patch with the MIDISTOMP diagnostics screen
before trusting it. Raising the firmware's DSP Full limit 225 -> 300 is a separate
firmware decision (about 39 % CPU headroom measured at declared 213).

**Order.** Cheapest first: EuGate, Sweep, Breather, S.GN_L, then the stereo-dry four
(WaveFold, DirtBox, DualShft, DubSiren with Pan), then Scrub, GRDELAY, Choral. Each
port keeps docs/SAFE-DSP-RULES.md, gets a host test (L and R stay apart, or dry stays
stereo) and a declared cost from a probe measurement.

**IDs.** GRDELAY, Breather and GRDLOOP replace BarDelay (495), Pump (493) and the old
Looper (497) on purpose; a user who keeps an old one installed next to its replacement
gets a clash. All pack effects stay in Delay 8 for v2.0 (see docs/EFFECT-IDS.md for the later plan).

## Bugs to fix in the stereo pass (MIDISTOMP v2.0 beta1, Luca's pedal, 2026-10-24)

- **GRDELAY: about 1 s audio dropout on a footswitch press with a stereo
  signal.** Mono doesn't do it. Suspects:
  - the on/off path handles R differently from L: the current mono code copies L
    to R, while the firmware's bypass passes R through;
  - a buffer clear or reset on toggle, or the Tail fade, muting both sides.

  Reproduce in a host test (stereo input, toggle on/off) before the GRDELAY port, and
  make sure the stereo port fixes it.

## Done: stereo port, part 1 (2026-10-28/29)

Host tests only (no pedal, no listening, no CPU measurement). Every effect: new `tests/<name>_stereo.c`
(L and R stay apart, or the dry stays stereo), the older tests adapted to the 16-float stereo block, ZDL built
with the TI compiler, `dsp_cost_comment` in the manifest marks the cost as an estimate. All pack tests pass;
the only failure in `py tests/run.py` is `looper_loop` (the Looper worker's). Covers untouched: no first-three
knob label changed. Shared helpers: `tests/stereo_entry.h` (fake pedal context), `tests/stereo_dry.h`
(Mix 0 / 50 / 100 check of the stereo-dry four).

| Effect | What | New knob | Declared cost (was -> now, estimate) | ZDL bytes (was -> now) |
|---|---|---|---|---|
| EuGate | one gate for both sides; the louder side drives Sync | none | 5 -> 6 | 14,110 -> 14,462 |
| Sweep | own all-pass / ring / filter state per side, one LFO | none | 17 -> 30 | 19,750 -> 20,422 |
| Breather | one pump envelope, own reverb set per side (right lines 23 samples longer) | none | 18 -> 32 | 16,314 -> 16,602 |
| S.GN_L | one loss decision and codec gains, own filters / buffers / noise per side; **Size now 1..400 ms** (was 500: two sets of four buffers must fit the 705 KB arena) | none | 18 -> 32 | 21,574 -> 23,494 |
| WaveFold | hears (L+R)/2, dry L/R apart, same wet on both | none | 26 | 9,374 -> 9,374 |
| DirtBox | as WaveFold | none | 38 | 25,274 -> 25,242 |
| DualShft | as WaveFold | none | 44 | 24,266 -> 23,594 |
| DubSiren | input passes in stereo, siren + echo mono, equal-power Pan (centre 0.707 each) | **Pan** (knob 9, L50..C..R50, default C) | 25 | 21,738 -> 23,226 |
| Scrub | MONO: one 7.9 s ring of (L+R)/2, dry L/R apart. ST: two 3.9 s rings. The loop is written out twice (macro) so MONO pays nothing for ST; knob change restarts the buffer | **Stereo** (knob 8, MONO/ST, default MONO) | 14 -> 25 | 18,478 -> 19,970 |
| GRDELAY | MONO (7.9 s), ST (ring per side, own filters, 3.9 s), PING (one ring, loop = 2 x Time, left = tap half way (odd repeats), right = loop tap (even repeats), Time max about 1.9 s). Knob change restarts the buffer | **Stereo** (knob 9, MONO/ST/PING, default ST) | 31 -> 44 (mid-way) | 23,218 -> 26,626 |
| Choral | tracker on (L+R)/2; each choir section (GIANT -0.9, MEN -0.4, WOMEN +0.4, KIDS +0.9 x Width) panned equal power, normalised so Width 0 = the old mono level | **Width** (knob 9, default 50) | 122 -> 135 | 28,866 -> 30,414 |

Notes:
- Choral grew to 30,414 bytes (ELF 30,338), under the 30.9 KB (ELF 30,818) known to boot. Watch it.
- Choral Width only spreads *sections*: a one-section choir (MEN, WOMEN, KIDS, GIANT) stays in the centre.
- Scrub / GRDELAY ST and PING cannot go back further than the 3.9 s ring: Scrub's Pos beyond that is held
  at the limit (the label still shows up to 6.00 s).
- Scrub MONO and GRDELAY MONO with a hard-panned source: (L+R)/2 is 6 dB down on that side, as for the
  other mono-sum effects.

### GRDELAY footswitch dropout (v2.0 beta1): what the host test found

Reproduced against the pre-port code (`git show HEAD:src/custom/griddly/griddly.c`, stereo input, footswitch off at
3 s and on at 5 s, all 6 engines, Tail ON/OFF, Mix 50/100):
- The effect function by itself never muted the dry at Mix 50. The only way to make it go silent for about
  a second on a press is **Mix 100 (no dry) after switching on with Tail ON, or with REVRS**: while off, Tail ON
  lets nothing into the ring, so for one Time after switching on there is nothing to echo (0.5 s at Time 496 ms,
  about 1 s at Time 1 s, 2 x Time in PING). Mono has it too; I could not make stereo worse in the effect code.
- The old code also overwrote R with L on every call, switched off too, so R was never passed through
  (an input on R only gave silence at all times). That is the part that depends on a stereo signal and the
  most likely other contributor to the report.
Port: R is never overwritten; switched off with Tail OFF the output of both sides is bit-exact the input, with
Tail ON it is the dry plus the decaying tail; and a **dry hold**: after switching on (Tail ON, or REVRS) the dry
is kept at full level for one Time and then fades to Mix's own over 50 ms, so no gap. `tests/griddly_stereo.c`
toggles the footswitch with a stereo noise input in 3 modes x 6 engines x Tail x Mix (72 runs): no cut longer
than 58 ms, longest 35 ms (the hand-over), and the old code gave 0.5 to 1 s holes in the same test.
**Not proven on the pedal.** If the pedal still cuts after this build, the cause is outside the effect (the
firmware's flip path), and the joint plan sends it back to the firmware.

### Needs the pedal

- Listen to every effect with a real stereo source; confirm none goes silent with only the R jack plugged.
- Measure the CPU of each (the declared costs are estimates, mono net x 1.8 + 25 %); GRDELAY ST and PING
  on a full patch with the MIDISTOMP diagnostics screen, then decide 44 vs about 56.
- GRDELAY footswitch with a stereo signal at several Mix / Tail / Time settings.
- Choral at 30.4 KB must still be listed and load.
- Scrub and GRDELAY: switching the Stereo knob while playing (the buffer restarts, dry for about 31 ms).
- The host test compiler here is `zig cc` through a gcc shim (no gcc on this PC); `sys/mman.h` is shimmed with
  VirtualAlloc below 4 GB. Run: `py tests/run.py` needs a real `gcc` on PATH, or the same shim.

### Left (not done in part 1)

- The Looper (other worker). Cover art for the new knobs (none needed: first-three labels unchanged).
- A listening pass on PING with REVRS / TAPS (they use the doubled loop; the half-way tap is a plain read).

