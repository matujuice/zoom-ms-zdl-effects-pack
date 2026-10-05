# CLAUDE.md

Project notes for Claude Code. Read this first, then the effect's own header comment: each `.c` file starts with a full description of its knobs and DSP.

Seven custom effects (WaveFold, DualShft, Choral, EuGate, DubSiren, S.GN_L, Scrub) for Zoom MS pedals, built as `.ZDL` files with the toolchain from themanro/ZoomMultistompZDL (`build/`). Owner's pedal: MS-60B running MS-50G firmware; other pedals are untested.

## Layout

- `src/custom/<effect>/`: `<effect>.c` (DSP), `manifest_pedal.json` (knobs, ranges, defaults, fxid), `make_cover.py`, `build.py`. Choral lives in `formant/`, S.GN_L in `sgnl/`.
- `src/airwindows/common/covers/*.json`: generated covers. `make_cover.py` writes them; `custom_covers.py` loads them.
- `tests/`: host tests (`python3 tests/run.py [name]`).
- `release/`: pack README, cover sheet. `make_release.py` zips `dist/*.ZDL` with them.
- `src/probes/<probe>/`: hardware probes, never released; `build_all.py` builds one only when named. TempoPrb (fxid 499) tests whether a custom effect can follow the pedal's tempo the TAPEECH3 way.
- fxids: DualShft 480, DubSiren 485, Choral 486, WaveFold 487, EuGate 488, S.GN_L 489, Scrub 490.
- S.GN_L: `effect_name` is SGNL (file SGNL.ZDL, symbols, SONAME); the manifest's `display_name` S.GN_L goes only into the descriptor name entry (LinkerConfig.display_name).

## Where to look

- Why an effect behaves as it does (owner's decisions, open items): `docs/DECISIONS.md`. Read its entry before changing an effect.
- Knobs as users see them: `docs/EFFECTS.md`, the README and the manifests.
- Bar sync from an iPhone, per-effect settings table: `docs/IPHONE-SYNC.md`.
- C rules in full: `docs/SAFE-DSP-RULES.md`. Pedal tempo research: `docs/TEMPO-SYNC.md`.

## Building and testing

- The TI C6000 compiler only exists on the owner's Windows PC, so Claude cannot build a `.ZDL` in the cloud. The owner double-clicks `build_<effect>.bat` and loads `dist\<Name>.ZDL` with Zoom Effect Manager.
- Claude's job is source, manifests, covers and host tests. After any DSP change run `python3 tests/run.py`.
- Say plainly what was and wasn't tested. Nothing here replaces listening on the pedal, and CPU use has never been measured.

## Rules for the C code

- No float-to-unsigned casts: the linker can't resolve `__c6xabi_fixfu`. Write `(unsigned int)(int)(x)`.
- No libm (`sqrtf`, `expf`, `fmodf`...). Use small helpers: `wf_rsqrt` in WaveFold, `exp2_oct` in DubSiren.
- Avoid float division and any integer division or modulo. EuGate builds its Euclidean pattern as a 64-bit mask by adding `Notes` and subtracting `Steps` on overflow. WaveFold's level match uses `ein * rsqrt(ein * elp)` instead of a divide.
- No `switch`, no static/const arrays in `.audio`, no `double`, no `long long`. Every helper is `FUNC_ALWAYS_INLINE` (macro `*_ALWAYS_INLINE`).
- `effect_name` is at most 8 characters.
- The pedal passes each knob as (screen value)/100 whatever its maximum. `sr_knob`, `ch_ui`, `wf_knob` convert back; copy that pattern for new knobs.
- Label callbacks (`ZDL_GetLabel_N`) see only their own knob's value. If a label depends on another knob, encode it in the knob's range (DubSiren's Rate does this: 101..112 are note values).
- State lives in the pedal-provided arena and is validated with a magic number. Change the magic whenever the state struct changes.
- Keep the 8-sample block structure: knob reads and `prepare` once per block, the sample loop after.

## Rules across the pack

- Tempo is the 8th knob on every synced effect (DualShft, Choral, EuGate, DubSiren, Scrub), so the Mozaic bar-sync script sends one knob number. Keep it there on any new synced effect.
- Every Tempo knob runs 0..441 and holds each BPM twice: 0..240 = BPM (below 40 = 40), 241..441 = twin copy, BPM = screen - 201. Flipping between a BPM and its twin restarts the effect with no tempo change. Read Tempo as raw x 100 up to 4.41 (`*_tempo_ui`), never through the 3.05 guess.
- Every effect except DubSiren has a Mix knob, a DJ-style crossfade: dry gain min(1, 2 - 2m), wet gain min(1, 2m). DubSiren adds its siren on top of the untouched input, so it has Vol instead.

## Covers

- 128 x 64, 1 bit. Previews are black on white. The screen shows the first three knobs: labels at y=37, firmware number boxes at y=46..61 (about 20 wide at x=14, 55, 96), the dial drawn by `_VSquash` under them.
- Pixels are 1.4 times taller than wide (`build/lcd_geometry.py`). Draw round things through `_VSquash` or divide y by 1.4, and check the preview stretched 1.4x.
- No bottom rule, except DubSiren, whose bottom edge belongs to its metal box.
- Use `Canvas` text helpers; knob labels come from the manifest names, so renaming a knob means re-running `make_cover.py` (EuGate's labels are set in its `LABELS`).

## Working habits

- Be concise with all of your responses.
- Reply in English. The owner sometimes writes Italian.
- Back up a file before a big edit. The owner may also edit files between sessions, so re-read before editing.
- When behaviour changes, update the header comment, the manifest comment, `docs/EFFECTS.md`, `docs/DECISIONS.md` and, if relevant, the README and `docs/IPHONE-SYNC.md`.
