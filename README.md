# Matujuice Zoom MS ZDL effects pack

Seven free custom effects for Zoom MultiStomp pedals, with source code.

![The seven covers as the pedal's screen shows them](release/covers.png)

| Effect | What it is |
|---|---|
| **WaveFold** | Buchla-style wavefolder with automatic level matching. |
| **DualShft** | Two tempo-synced pitch shifters, each with its own echo time (free or synced), and an LFO that bends them in opposite directions. |
| **Choral** | Turns the input into a vowel choir: five voices, 35 chords, 11 ways for the vowels to move. |
| **EuGate** | Euclidean rhythm gate. Steps and Notes up to 64 each, so polymeters are possible. Swing, Gap and Soft too. |
| **DubSiren** | A dub siren on the footswitch with a tape-style echo. The rate can sync to tempo. |
| **S.GN_L** | A broken digital line: packets drop out and get replaced by silence, a buzzing replay, a fade or hiss, with codec damage on top. |
| **Scrub** | Records the last 7.9 seconds and scrubs the last 6; a knob moves a read head through them, and where you stop, the grain under the head loops forever (a freeze). |
| **DirtBox** (new, not in a release yet) | One distortion, three models (ACID 303-box grit, RAT, METAL), with an automatic noise reducer that keeps kick tails. |

Every knob is explained in [docs/EFFECTS.md](docs/EFFECTS.md).

**Sync to the bar:** the pedal ignores MIDI clock, but a Mozaic script on an iPhone can restart DualShft, Choral, EuGate, DubSiren and Scrub on every downbeat. Get the script on [PatchStorage](https://patchstorage.com/zoom-ms-bar-sync-keep-custom-zoom-effects-on-the-beat/) (or [tools/mozaic/](tools/mozaic/)); setup and the best knob settings for it (for example EuGate Reset SYNC, DubSiren Trig SHold or SPuls) are in [docs/IPHONE-SYNC.md](docs/IPHONE-SYNC.md).

**Tested on:** a Zoom MS-60B running MS-50G firmware. The MS-50G, MS-70CDR and other MS pedals are untested; if you try one, please open an issue and say whether the effects load and work.

Unofficial. Not affiliated with or endorsed by Zoom. Use at your own risk.

## Install

1. Download the latest pack from the [Releases](../../releases) page and unzip it.
2. Load the `.ZDL` files with Zoom Effect Manager ("Read Effects from folder"). Step by step: [docs/INSTALLING-ZDLS.md](docs/INSTALLING-ZDLS.md).

Each effect has its own ID (480 DualShft, 485 DubSiren, 486 Choral, 487 WaveFold, 488 EuGate, 489 S.GN_L, 490 Scrub, 491 DirtBox). If another custom effect on your pedal uses one of these numbers, change `fxid` in that effect's `manifest_pedal.json` and rebuild.

## Build from source

You need Python 3 and the TI C6000 compiler (`ti-cgt-c6000_8.5.0.LTS`, free from Texas Instruments). The build scripts look in the usual install places (`C:\ti`, your Downloads folder, `/Applications/ti`); otherwise set `TI_CGT_ROOT` to the folder that contains `bin` and `include`.

```
py build_all.py            # build them all into dist/
py build_all.py eugate     # build one
py make_release.py         # zip dist/*.ZDL with the readme and licence into release/
```

Effect names for `build_all.py`: `wavefold`, `dualshft`, `formant` (Choral), `eugate`, `dubsiren`, `sgnl` (S.GN_L, file `SGNL.ZDL`), `scrub`, `dirtbox`.

## Repository layout

```
src/custom/<effect>/   DSP source (.c), manifest_pedal.json (knobs, defaults, ID), make_cover.py, build.py
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
