# Matujuice Zoom MS ZDL effects pack

Free custom effects for Zoom MultiStomp pedals, with source code.

![The covers as the pedal's screen shows them](release/covers.png)

| Effect | What it is |
|---|---|
| **WaveFold** | Buchla-style wavefolder with automatic level matching. |
| **DualShft** | Two tempo-synced pitch shifters, each with its own echo time (free or synced), and an LFO that bends them in opposite directions. |
| **Choral** | Listens to the note your synth plays and sings it as a choir: men, women, kids or giants in 15 combinations, 1 to 6 singers, in unison, intervals or chords, on vowels, syllables, swells or a canon in time. For mono synth lines (one oscillator). |
| **EuGate** | Euclidean rhythm gate. Steps and Notes up to 64 each, so polymeters are possible. Swing, Gap and Soft too. |
| **DubSiren** | A dub siren on the footswitch with a tape-style echo. The rate can sync to tempo. |
| **S.GN_L** | A broken digital line: packets drop out and get replaced by silence, a buzzing replay, a fade or hiss, with codec damage on top. |
| **Scrub** | Records the last 7.9 seconds and scrubs the last 6; a knob moves a read head through them, and where you stop, the grain under the head loops forever (a freeze). |
| **GridDly** | One tempo-synced delay with six engines on a Type knob (clean, tape, dub, reverse, multitap, lo-fi), Duck, and a Tail switch so repeats ring out when you switch it off. New, not in a release yet. |
| **SyncEQ** | EQ with low and high cut, three bands and a little drive, which also passes the iPhone bar sync on to the effects in later slots (4 to 6 too). New, not in a release yet. |
| **Breather** | Tempo-synced pump with a built-in reverb: duck, gate or swell your sound, the reverb, both, or what feeds the reverb, on every beat. New, not in a release yet. |
| **DIRTBOX** | A distortion box for a 303: seven real pedal circuits (TS9, Distortion+, DS-1, RAT 2, Big Muff, Super-Fuzz, Metal Zone), a 3-band acid EQ and an automatic noise reducer. New, not in a release yet. |
| **Sweep** | Phaser, flanger and resonant filter in one effect: Type picks the engine, one LFO sweeps it, from a 1/32 note up to 8 bars, locked to the bar. New, not in a release yet. |

Every knob is explained in [docs/EFFECTS.md](docs/EFFECTS.md).

**Sync to the bar:** the pedal ignores MIDI clock, but a Mozaic script on an iPhone can restart DualShft, Choral, EuGate, DubSiren, Scrub, Breather, GridDly and Sweep on every downbeat; with the new builds, effects in slots 4 to 6 follow too (Tempo on FOLLW). Get the script on [PatchStorage](https://patchstorage.com/zoom-ms-bar-sync-keep-custom-zoom-effects-on-the-beat/) (or [tools/mozaic/](tools/mozaic/)); setup and the best knob settings for it (for example EuGate Reset SYNC, DubSiren Trig SHold or SPuls) are in [docs/IPHONE-SYNC.md](docs/IPHONE-SYNC.md).

**Tested on:** a Zoom MS-60B running MS-50G firmware. The MS-50G, MS-70CDR and other MS pedals are untested; if you try one, please open an issue and say whether the effects load and work.

Unofficial. Not affiliated with or endorsed by Zoom. Use at your own risk.

## Install

1. Download the latest pack from the [Releases](../../releases) page and unzip it.
2. Load the `.ZDL` files with Zoom Effect Manager ("Read Effects from folder"). Step by step: [docs/INSTALLING-ZDLS.md](docs/INSTALLING-ZDLS.md).

The effects marked new are on `main` but not in a release yet: take the tested builds from [dist/](dist/) or build them from source (below) until the next release.

Each effect has its own ID (480 DualShft, 485 DubSiren, 486 Choral, 487 WaveFold, 488 EuGate, 489 S.GN_L, 490 Scrub, 491 DirtBox, 493 Breather, 494 SyncEQ, 495 GridDly, 496 Sweep). If another custom effect on your pedal uses one of these numbers, change `fxid` in that effect's `manifest_pedal.json` and rebuild.

## Build from source

You need Python 3 and the TI C6000 compiler (`ti-cgt-c6000_8.5.0.LTS`, free from Texas Instruments). The build scripts look in the usual install places (`C:\ti`, your Downloads folder, `/Applications/ti`); otherwise set `TI_CGT_ROOT` to the folder that contains `bin` and `include`.

```
py build_all.py            # build them all into dist/
py build_all.py eugate     # build one
py make_release.py         # zip dist/*.ZDL with the readme and licence into release/
```

Effect names for `build_all.py`: `wavefold`, `dualshft`, `formant` (Choral), `eugate`, `dubsiren`, `sgnl` (S.GN_L, file `SGNL.ZDL`), `scrub`, `synceq`, `breather`, `dirtbox`, `sweep`, `griddly`. Probes (`tempoprb`, `dryprb`) are built only when named.

## Repository layout

```
src/custom/<effect>/   DSP source (.c), manifest_pedal.json (knobs, defaults, ID), make_cover.py, build.py
src/custom/common/     drytag.h, the bar tag that carries bar sync to later slots
src/airwindows/common/ shared cover and parameter helpers; covers/*.json are the generated covers
build/                 the ZDL linker and tools (from ZoomMultistompZDL, see Credits)
docs/                  knob reference, install guide, DSP rules
tests/                 host tests: python3 tests/run.py
tools/mozaic/          iPhone (Mozaic) bar sync (.mozaic + text) and a USB connection test script
```

To change a cover, edit that effect's `make_cover.py` and run it; it rewrites the cover JSON and a preview image. The pedal's pixels are 1.4 times taller than wide, so round shapes are drawn squashed.

## Credits

- Built on the toolchain from [themanro/ZoomMultistompZDL](https://github.com/themanro/ZoomMultistompZDL) (MIT, Copyright (c) 2026 Roman Rusinov). The `build/` folder and the install and DSP-rule docs come from there; the upstream repo has much more documentation.
- WaveFold's fold curve is from "Virtual Analog Buchla 259 Wavefolder" (Esqueda, Pontynen, Valimaki, Parker; DAFx-2017).
- The effects themselves are original work.

## Licence

MIT, see [LICENSE](LICENSE).
