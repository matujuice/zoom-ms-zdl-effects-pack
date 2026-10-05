# CLAUDE.md

Directory only. Read the effect's header comment (each `.c` starts with its knobs and DSP) before touching it.

## What this is
Seven custom Zoom MS effects (WaveFold, DualShft, Choral, EuGate, DubSiren, S.GN_L, Scrub) built as `.ZDL` with themanro/ZoomMultistompZDL (`build/`). Owner's pedal: MS-60B on MS-50G firmware; others untested. Effects are for synths/drums, never guitar.

## Where things are
- `src/custom/<effect>/`: `<effect>.c`, `manifest_pedal.json`, `make_cover.py`, `build.py` (Choral in `formant/`, S.GN_L in `sgnl/`).
- `src/airwindows/common/covers/*.json`: generated covers. `src/probes/`: hardware probes, never released.
- `tests/`: `python3 tests/run.py [name]`. `release/` + `make_release.py`: pack zip.
- fxids: DualShft 480, DubSiren 485, Choral 486, WaveFold 487, EuGate 488, S.GN_L 489, Scrub 490, TempoPrb probe 499.
- S.GN_L: `effect_name` SGNL, display name S.GN_L only in the descriptor name entry.
- Docs: `docs/SAFE-DSP-RULES.md` (C rules), `docs/DECISIONS.md` (owner decisions per effect), `docs/COVERS.md` (cover rules), `docs/TEMPO-SYNC.md`, `docs/IPHONE-SYNC.md`, `docs/EFFECTS.md`.

## Building and testing
No C6000 compiler in the cloud; builds happen on the owner's PC. Cloud work is source, manifests, covers, host tests. After any DSP change run `python3 tests/run.py`. State what was and wasn't tested; CPU use never measured.

## C rules that each broke a build (full list: docs/SAFE-DSP-RULES.md)
- No float-to-unsigned casts: use `(unsigned int)(int)(x)`.
- No libm, no float/integer division or modulo, no `switch`, no static/const arrays in `.audio`, no `double`/`long long`.
- Helpers are `FUNC_ALWAYS_INLINE`. `effect_name` max 8 chars.
- Knobs arrive as screen/100; convert back like `sr_knob`/`ch_ui`/`wf_knob`.
- Labels see only their own knob. State magic number changes with the state struct. Keep the 8-sample block structure.

## Conventions
- Mix = DJ crossfade (dry min(1,2-2m), wet min(1,2m)); DubSiren has Vol, no Mix.
- Tempo is knob 8 on every synced effect, runs 0..441 with twin BPM copies (see docs/TEMPO-SYNC.md); read raw x100.
- Covers: see docs/COVERS.md.
- Change a knob or behaviour: update header comment, manifest, docs/EFFECTS.md, README, docs/IPHONE-SYNC.md table in step.

## Habits
Back up before big edits; re-read files (owner edits between sessions). Short English replies; owner sometimes writes Italian.

## Open items
Sound preview for README/Reddit; ask MS-50G/MS-60B/MS-70CDR owners to confirm effects load.
