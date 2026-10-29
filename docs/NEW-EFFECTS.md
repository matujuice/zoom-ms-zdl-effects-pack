# New effects: plan (Luca, 2026-10-23)

Not built yet. Five new effects, built after the stereo port (docs/STEREO.md). All are
stereo in and out, keep docs/SAFE-DSP-RULES.md, get host tests, and use at most 9 knobs.
Costs are estimates: re-measure on the pedal and set `dsp_cost` from that.

**Mix:** every new effect has a Mix knob, and it is the pack's DJ crossfade
(docs/DECISIONS.md, themanro style): dry gain min(1, 2 - 2m), wet gain min(1, 2m), so
dry is full up to 50, wet is full from 50, and both are full at 50 (Luca, 2026-10-24).

**Defaults and names** (Luca, 2026-10-24): every knob that can be off is off at 0 and
defaults to off; Mix defaults to 100; knob names reuse the pack's existing names wherever
one fits (Time, Fdbk, Reso, Mix ...). Effect names on the pedal (`effect_name`, Luca, 2026-10-24): LIMITER = **LOUDER**,
DJFILTER = **DJFilter**, WASH = **Washout**, RISER = **Takeoff**, COMP = **Gooey**. The
section names below are working names only.

**IDs:** all pack effects are in Delay (gid 8): 480-481, 484-497, 501 (498-500 are probes,
482/483 reserved). Use the next free number there. Moving to a 900+ range is a proposal for
after v2.0: see [EFFECT-IDS.md](EFFECT-IDS.md).

**Build order:** Limiter, DJFilter, Wash (the v2.0 brief's master chain), then Riser, then Compressor.

## LIMITER: mastering loudness limiter

The limiter for the end of a live set's chain, after `[MIX]`. Modelled on the
transparent look-ahead limiters used to master and play live sets (Pro-L / Ozone
Maximizer class): it pushes the level up and holds it under a ceiling.

- **Linked stereo, always.** One gain for both sides, from the louder side. No Link knob (Luca, 2026-10-24).
- **Look-ahead** about 1.5 ms. The gain reaches its target before the peak arrives,
  so no clipping and no click. The delay doesn't matter at the end of the chain.
- **Release.**
  - Program-dependent: a fast stage for single peaks and a slow stage for sustained
    level, so it doesn't pump.
  - AUTO picks between them.
- **Ceiling:** a hard safety clip at the ceiling after the gain stage.
- **Knobs:**

  | # | Knob | Range |
  |---|---|---|
  | 1 | Gain | 0 to +18 dB |
  | 2 | Ceil | ceiling, -6.0 to 0.0 dB, default -0.1 |
  | 3 | Rels | release, AUTO (default), 1-1000 ms |
  | 4 | Style | CLEAN (default) / PUNCH / LOUD |
  | 5 | Mix | DJ crossfade, default 100 (parallel limiting) |

  The dry path is delayed by the same look-ahead, so a part Mix never comb-filters.
  The ceiling clip runs after Mix, so the ceiling holds at any Mix. MIDISTOMP's own
  output safety limiter stays on after it as a backstop (it does nothing below 0 dB).

  Style sets only the saturation and how much transient gets through (Rels owns the
  release): CLEAN has none, PUNCH lets transients through a little, LOUD is the densest.
- **Cost:** about 10.
- Built 2026-10-29: fxid 481, cost 10 (estimate), ZDL size 13826 bytes (with the live meter on the cover; was 11954).

## DJFILTER: DJ filter and kick kill

Its own effect (Luca, 2026-10-24), not a Sweep mode. Sweep stays the LFO-modulated
filter. This one is the static filter you ride by hand, CC or Perform: the v2.0 brief's
"master high-pass" and the kick kill for transitions. Modelled on DJ mixer filters
(Xone / DJM style).

- **Three filter knobs, no mode switch** (Luca, 2026-10-24). The knobs are encoders
  with no detents, so each knob is off at 0 and does one thing:
  - **LP** 0-100: 0 is completely off, 100 the most extreme (closes down to about 40 Hz).
  - **HP** 0-100: 0 is completely off, 100 the most extreme (closes up to about 10 kHz).
  - **DJ** -100 ... 0 ... +100: the one-knob DJ mixer filter, left LP, right HP, 0 off;
    -3..+3 is also flat, so centre is easy to find.
  - LP and HP sweep the same way: turning right always means more filtering
    (exponential in frequency).
  - **How they combine:** DJ drives the same two filters, and the more closed setting
    wins: LP amount = max(LP, DJ below 0), HP amount = max(HP, DJ above 0). So LP and HP
    can be parked as a floor and DJ sweeps past them; LP + HP together give a band-pass.
  - A filter whose amount is 0 is skipped, so it costs nothing.
- **Resonance follows the sweep**, like a DJ filter: about none when the filter is
  open, rising as it closes (LP going down, HP going up). The Reso knob sets how much
  it reaches at the far end. Capped below self-oscillation.
- **Kill:** an independent steep low cut (24 dB/oct) at a set frequency. It removes the
  kick and bass, the classic "kill the low end before the drop". Use it on its own or on
  top of the filter.
- **Footswitch:** normal on/off, so a press is the kick kill (for example Kill 120 Hz
  with the filter open). The switch-over is ramped over a few ms so it never clicks.
- **Smoothing:** LP, HP and DJ moves are smoothed (Glide), so a fast CC or Perform jump sweeps
  instead of stepping.
- **Structure:** linked stereo, state-variable or biquad filters (2 per side for 24
  dB/oct), coefficients updated once per block, not per sample.
- **Knobs:**

  | # | Knob | Range |
  |---|---|---|
  | 1 | LP | 0 (off) to 100 |
  | 2 | HP | 0 (off) to 100 |
  | 3 | DJ | -100 (LP) ... 0 (off) ... +100 (HP) |
  | 4 | Reso | 0-100, resonance at the far end of the sweep, default 30 (DJ-filter sound; an exception to the off-by-default rule) |
  | 5 | Kill | OFF, 20-300 Hz, default OFF |
  | 6 | Slope | 12 / 24 dB per octave (filter only), default 24: how steeply sound past the cutoff is cut. 12 is gentle and warm, 24 is the hard DJ-mixer cut |
  | 7 | Glide | 0-1000 ms, how fast LP/HP/DJ follow the knob; default about 20 ms, just enough that knob steps never click |
  | 8 | Level | output trim, -12 to +6 dB |
  | 9 | Mix | DJ crossfade, default 100 |

- **Cost:** about 6 with one filter on, about 9 with LP, HP and Kill all on.
- Built 2026-10-29: fxid 484, cost 9 (estimate, all on), ZDL size 15978 bytes.

## WASH: dedicated washout

Not a GridDly engine: its own effect, built to do one thing well and stay cheap. It
turns the input into a smeared, reverb-like cloud of repeats, the Octatrack-style
"washout" for transitions.

- **Structure:**
  - Tempo-synced stereo feedback delay, 16-bit buffers.
  - A chain of 4 all-pass diffusers inside the feedback loop: each pass smears the
    sound more.
  - Slow modulation on the diffusers.
  - HiCut and LoCut filters in the loop.
  - Feedback can go past 100, with a soft clip.
- **Footswitch:** plain effect on/off with trails (Luca, 2026-10-24): OFF stops new input
  going in and lets the tail ring out; the dry signal is untouched.
- **Knobs:**

  | # | Knob | Range |
  |---|---|---|
  | 1 | Time | same as GridDly's Time: 12 ms..1.00 s, then 1/32..1bar, 2bar |
  | 2 | Wash | diffusion amount |
  | 3 | Fdbk | 0-120, as GridDly: 100 = 1:1 (holds forever with Send 0 and both cuts off), above 100 grows, soft-clipped |
  | 4 | HiCut | low-pass in the loop: 0 off, 100 darkest |
  | 5 | LoCut | high-pass in the loop: 0 off, 100 thinnest |
  | 6 | Mod | modulation depth |
  | 7 | Width | stereo width |
  | 8 | Send | how much input feeds the wash (0 = only the tail keeps ringing) |
  | 9 | Mix | DJ crossfade, default 100 |

  Defaults: Time 1/4, Wash 70, Fdbk 60, HiCut and LoCut 0 (off), Mod 20, Width 100,
  Send 100 (an exception: at 0 it would make no sound), Mix 100.

  No Freeze knob (Luca, 2026-10-24): the all-pass diffusers lose no level, so Fdbk 100
  with Send 0 and both cuts off already holds the cloud. Knob names and ranges match the
  pack (Time and Fdbk as GridDly); every knob that is off is off at 0.

  Send replaces Duck (Luca, 2026-10-23): turning Send up and back down is the "throw"
  move, and it reaches CC and Perform. A washout is meant to take over, so ducking matters
  less here. Send is smoothed so a fast move doesn't click.

- **Cost:** about 25-30.
- Built 2026-10-29: fxid 492, cost 28 (estimate), ZDL size 21430 bytes.

## RISER: risers and downlifters (Luca, 2026-10-24)

Only adds sound on top, like DubSiren: the input passes through in stereo, never touched
(filtering the mix is DJFilter's job). No Mix knob, Vol instead (as DubSiren).

- **Sounds:** NOISE is white noise through a resonant filter that sweeps with it;
  SAW is 3 detuned saw voices spread left and right; SINE is one sine (with DOWN and a
  low Range, the sub drop). The volume swells with the sweep; the stereo width grows;
  the curve is fixed exponential.
- **Dir:** UP sweeps up and cuts clean at the end (the drop); DOWN starts at the top,
  falls and fades to silence.
- **Space:** a reverb tail on the riser's own sound only, ringing on after the cut.
  Reuse WASH's diffuser code.
- **Key:** AUTO listens to the input (strongest note over the last few bars, from a
  decimated chroma) and the sweep starts and ends on that note; with no clear note
  (silence, drums only) it falls back to FREE. FREE is an untuned glide. C to B fixes the
  note. The listener runs only in AUTO (about 3-5 cost); FREE and fixed keys cost nothing.
- **Trigger (Trig):** PLAY or MAN. No setting needed; the footswitch tells a press from a hold.
  - **PLAY, short press** (let go within half a second): one full sweep over Length, on its
    own; UP cuts on the bar. Pressing again during a sweep drops it early.
  - **PLAY, hold** (still down after half a second): manual riser. It rises while you hold
    (at Length's speed), stays at the top if it gets there, and drops the moment you let go.
  - **MAN:** no automatic sweep; Pos is the position (0 rest, 100 top), by hand, CC or
    Perform; smoothed.
  - **What the effect sees:** its on/off flag (passed while OFF, as DubSiren) plus a
    "footswitch down" flag from the firmware (new, see Firmware below). Any on/off flip
    starts a sweep; if the down flag is still set after half a second it is a hold, and
    the release drops it. MIDI: an on/off CC flip = a press; a momentary CC (on while
    held, off on release) = a hold, since an OFF during a sweep drops it.
- **Idle:** when nothing sounds and the tail has died, skip all processing (about zero
  cost); only the AUTO key listener keeps running.
- **Knobs:**

  | # | Knob | Range |
  |---|---|---|
  | 1 | Dir | UP / DOWN |
  | 2 | Len | length: 1/2/4/8/16 bars, or FREE seconds |
  | 3 | Trig | PLAY (default) / MAN |
  | 4 | Sound | NOISE / SAW / NOISE+SAW / SINE / NOISE+SINE |
  | 5 | Range | 1-4 octaves |
  | 6 | Key | AUTO (default) / FREE / C to B |
  | 7 | Space | reverb tail, 0 off |
  | 8 | Pos | manual position 0-100 (MAN only) |
  | 9 | Vol | level of the added sound |

- **Cost:** about 12-15 while sounding, plus 3-5 for AUTO key.
- **Firmware (needed for hold):** MIDISTOMP passes RISER a "footswitch down" flag (the
  footswitch hold hook, asm/looper/hold_trampoline.S, already sees press and release).
  Without it, a short press works and hold-to-rise works only via Perform's footswitch
  Hold -> on / Release -> off, or a momentary MIDI CC. No SETTINGS option involved.
- **Firmware follow-up (not in this build):** RISER's footswitch LED and Perform softkey
  light should show the sweep: brightness fading up on a rise, down on a downlifter, off
  when the tail ends. That needs MIDISTOMP's press/LED service (today owned only by
  GRDLOOP) to take a second owner; it touches the same claim code as the
  looper-bar-over-other-effects bug. QUANT (start on the next bar) can come back with it.
- Built 2026-10-29: fxid 494, cost 20 (estimate, AUTO total), ZDL size 21846 bytes.

## COMP: mix bus compressor

Modelled on the classic SSL G-bus style VCA compressor used to glue mixes. One
character, done well. Trimmed to 7 knobs (Luca, 2026-10-23):
no Mode (three characters would triple the tuning work) and no Level (MkUp already
sets the output). Knob names fit the 5-character LCD and match LIMITER (Rels in both).

- **Detector:** linked stereo, feed-forward, RMS/peak blend.
- **Knee:** soft.
- **Sidechain:** high-pass on the detector only (the audio is untouched), so the kick doesn't pump the whole mix. A knob (Luca, 2026-10-23).
- **Knobs:**

  | # | Knob | Range |
  |---|---|---|
  | 1 | Thrsh | threshold, -40 to 0 dB, default 0 (off) |
  | 2 | Ratio | 1.5, 2, 4 (default), 10 |
  | 3 | Attck | attack, 0.1, 0.3, 1, 3, 10 (default), 30 ms |
  | 4 | Rels | release, 0.1, 0.3, 0.6, 1.2 s, AUTO (default) |
  | 5 | MkUp | makeup: AUTO / OFF (default) / +0.5 to +20 dB. AUTO adds back the average gain reduction |
  | 6 | Mix | linear dry/wet like the Digitakt 1 compressor (Luca, 2026-10-29; not the DJ crossfade), default 100 (parallel compression) |
  | 7 | SC HP | sidechain high-pass: OFF (default), 20-300 Hz |

  AUTO release is program-dependent, like the G-bus.
- **Cost:** about 8.
- Built 2026-10-29: fxid 501, cost 8 (estimate), ZDL size 19286 bytes (with the live meter on the cover; was 15886).

## Covers: not in this build (Luca, 2026-10-23)

Cover art is designed later with Luca, after some back and forth. Until then, each new
effect gets a plain placeholder cover from the existing cover tool (title and knob labels
only).

LIMITER (LOUDER) and COMP (Gooey) have the live meter on their placeholder cover: a band
(one 8-row block) between the title and the knob labels, drawn by the effect itself
(src/custom/common/zdl_meter.h; see Readouts). Host tests decode the cover after band
writes and render preview PNGs at a few gain-reduction levels.

## Readouts (LIMITER and COMP)

Both effects show how hard they are working, like the gain-reduction meter on a real
limiter or bus compressor. The effect draws the meter itself, on its own cover (Luca,
2026-10-23), so MIDISTOMP needs no new code or space for it. This uses themanro's
findings (github.com/themanro/ZoomMultistompZDL, docs/pedal-research/animated-covers.md,
from Leemuzhko's SDK):

- **Cover in RAM:** an effect may rewrite bytes of its own cover, and the firmware
  re-reads the cover on every redraw (probe C). The meter area is stored as literal
  (not run-length) bytes, so a rewrite never changes the encoded length.
- **Done (2026-10-29):** the linker resolves `extern unsigned char _<audio>_Pic[]` to
  the cover; build.py checks the band is literal and writes `<effect>_layout.h`. Bit 7 of
  every band byte is a dotted baseline so neighbours always differ (no accidental RLE run).
- **Redraw trigger:** the audio code posts the UI redraw event (Blink probe D:
  `Event_post` `0xC00CBF40`, MS-70CDR 2.10 addresses).
- **What it shows:** a gain-reduction bar plus the dB number, peak held about 300 ms.

**Rules:**
- Post a redraw at most about 15 times a second, only when the shown value changes,
  and only while this effect's page is on screen. Limit today: we cannot tell yet whether
  the page is on screen, so a change posts a redraw of whatever page shows (UI task cost,
  not audio). Steady value or effect off: no writes, no calls. ponytail: find the
  "current slot" address later.
- Check the firmware once, at state init (first 20 bytes of `Event_post` match MS-70CDR
  2.10, event handle in RAM). Without a match no OS call is ever made: the band is still
  written, the cover just stays static. It works on any MS-70CDR 2.10, stock included, not
  only MIDISTOMP.
- Never use the edit handlers' UI postbox (`state[7]`) from audio code, because it
  deadlocks.
- Test on the pedal for clicks or stalls with the meter running and other effects busy.
