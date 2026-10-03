# CLAUDE.md

Project notes for Claude Code. Read this first, then the effect's own header comment (each `.c` file starts with a full description of its knobs and DSP).

## What this is

Five custom effects (WaveFold, DualShft, Choral, EuGate, DubSiren) for Zoom MS pedals, built as `.ZDL` files with the toolchain from themanro/ZoomMultistompZDL (`build/`). Owner's pedal: MS-60B running MS-50G firmware. Other pedals are untested. The README, docs/EFFECTS.md and the manifests all describe the knobs, so keep them in step when a knob changes.

## Layout

- `src/custom/<effect>/`: `<effect>.c` (DSP), `manifest_pedal.json` (knobs, ranges, defaults, fxid), `make_cover.py`, `build.py`. Choral lives in `formant/`.
- `src/airwindows/common/covers/*.json`: generated covers. `make_cover.py` writes them; `custom_covers.py` loads them.
- `tests/`: host tests (`python3 tests/run.py [name]`).
- `release/`: pack README, cover sheet. `make_release.py` zips `dist/*.ZDL` with them.
- fxids: DualShft 480, DubSiren 485, Choral 486, WaveFold 487, EuGate 488, S.GN_L 489 (PR #2), Scrub 490.
- Scrub is not in `release/` or `make_release.py` until it has been tried on the pedal.

## Building and testing

The TI C6000 compiler only exists on the owner's Windows PC, so Claude cannot build a `.ZDL` in the cloud. The owner double-clicks `build_<effect>.bat` and loads `dist\<Name>.ZDL` with Zoom Effect Manager. Claude's job is source, manifests, covers and host tests. After any DSP change run `python3 tests/run.py`. Say plainly what was and wasn't tested; nothing here replaces listening on the pedal, and CPU use has never been measured.

## Rules for the C code (these have each caused a real failure or are in docs/SAFE-DSP-RULES.md)

- No float-to-unsigned casts: the linker can't resolve `__c6xabi_fixfu`. Write `(unsigned int)(int)(x)`. (This broke the EuGate build once.)
- No libm (`sqrtf`, `expf`, `fmodf`...). Use small helpers: `wf_rsqrt` in WaveFold, `exp2_oct` in DubSiren.
- Avoid float division and any integer division or modulo. EuGate builds its Euclidean pattern as a 64-bit mask by adding `Notes` and subtracting `Steps` on overflow. WaveFold's level match uses `ein * rsqrt(ein * elp)` instead of a divide.
- No `switch`, no static/const arrays in `.audio`, no `double`, no `long long`. Every helper is `FUNC_ALWAYS_INLINE` (macro `*_ALWAYS_INLINE`).
- `effect_name` is at most 8 characters.
- The pedal passes each knob as (screen value)/100 whatever its maximum. `sr_knob`, `ch_ui`, `wf_knob` convert back; copy that pattern for new knobs.
- Label callbacks (`ZDL_GetLabel_N`) see only their own knob's value. If a label depends on another knob, encode it in the knob's range (DubSiren's Rate does this: 101..112 are note values).
- State lives in the pedal-provided arena and is validated with a magic number. Change the magic whenever the state struct changes.
- Keep the 8-sample block structure: knob reads and `prepare` once per block, the sample loop after.

## Covers

- 128 x 64, 1 bit. Previews are black on white. The screen shows the first three knobs: labels at y=37, firmware number boxes at y=46..61 (about 20 wide at x=14, 55, 96), the dial drawn by `_VSquash` under them.
- Pixels are 1.4 times taller than wide (`build/lcd_geometry.py`). Draw round things through `_VSquash` or divide y by 1.4, and check the preview stretched 1.4x.
- No bottom rule on WaveFold, DualShft, Choral, EuGate. DubSiren's bottom edge belongs to its metal box and stays (Luca, 2026-10-03: DubSiren works differently from the others, so its framed cover stays as it is).
- Use `Canvas` text helpers; knob labels come from the manifest names, so renaming a knob means re-running `make_cover.py` (EuGate's labels are set in its `LABELS`).

## How the effects were shaped (decisions the owner made)

- Choral: 5 voices of 3 formant band-pass filters; main voice gets vibrato, drift, flutter and breath to sound human; auto-level keeps the output matched to the input. Chord 0 off, 1..42 detune, 43..48 whole semitones 2..7, 49..83 the 35 chords.
- DualShft: tempo-synced LFO on two pitch voices, free echo times.
- DubSiren: siren into a tape-style echo; Rate 101..112 sync to the Tempo knob, Fast = 2x and Slow = 0.5x of the note value; echo Time is never synced; Fdbk 0 = echo off, default 70; siren level is 0.3 x full-scale at Vol 100.
- EuGate: Steps 1..64 and Notes 1..64 (polymeters); Gap puts a small silence before a touching note; Mix stays on every effect; the cover is a 16-dot Euclid ring (round on the device), ghost "CLIDIAN", "5/16=3.3.3.3.4".
- WaveFold: Buchla 259 fold curve; the level match follows the input directly (it used to creep back slowly after a quiet decay, which made notes swell in).
- Scrub: 6 s 16-bit buffer in the arena (cleared 2048 samples per block after loading); Pos scrubs a read head over the last 4 s in 400 steps of 10 ms (0 = 4 s ago, 400 = now, label "0ms" (not NOW); read as raw x 100, not through the 3.05 guess; Luca dropped the Range knob 2026-10-03 for this), Glide (the brief called it Smooth; 5-letter names like the rest of the pack) glides it; grains always play at normal speed, so the pitch never bends (Luca chose constant pitch over pushing running grains with the head, 2026-10-03); the freeze is a crossfaded loop (new voice fades in over the first quarter of each Grain while the old fades out), not a 50 % overlap cloud, which beat on held tones; Dir FWD / REV / PING / RAND (ping-pong voices alternate and turn round on the same sample, so wherever a voice turns round (PING always, RAND on a coin flip) the seam crossfade is 1 ms instead of the quarter-grain one, which cancelled against its own mirror image); Grain 101..112 are note values synced to a Tempo knob (8th knob, 40..240 BPM), a long synced grain pulls the head in to fit the 6 s; Spray scatters grains either side of the head, up to +-250 ms, label "+-N" in ms (Luca, 2026-10-03); Rec LIVE / HOLD / STOMP (STOMP records while switched off, relies on the pedal passing audio to a bypassed effect: unverified). Concept: /mnt/project-files/ideas/effect-ideas.md.
- Every effect has a Mix knob.

## Working habits

- Back up a file before a big edit. The owner may also edit files between sessions, so re-read before editing.
- When behaviour changes, update the header comment, the manifest comment, docs/EFFECTS.md and, if relevant, the README.
- Keep replies short and in English. The owner sometimes writes Italian.

## Open items

- Sound preview (audio or video) for the README and the Reddit post.
- Ask owners of an unmodified MS-50G, MS-60B or MS-70CDR to report whether the effects load and work.
