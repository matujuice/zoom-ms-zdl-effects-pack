# Effect IDs

> **Proposal for after v2.0, not used yet.** v2.0 keeps the beta1 IDs: every pack effect is in
> Delay (gid 8) with its old fxid (480-497, 501). The plan for categories and IDs is in the firmware
> repo, zoom-ms-modding `docs/dev/future-effect-ids-and-categories.md`.

**Proposed rule (not in force): pack effect IDs (fxid) live in 900-1023.** Keep the effect's group
(gid); pick the next free number in 900-1023. Probes (498-500) and the old aliases 482/483 stay where they are.

## Stock IDs

Full table: [effect-ids.csv](effect-ids.csv) (gid, category, fxid, patch_id, name, file, source).
Read from every ZDL in the local stock updaters (2026-10-29):

| Updater | ZDLs |
|---|---|
| MS-50G v3.10 | 101 |
| MS-60B v2.10 | 143 |
| MS-70CDR v2.10 | 137 |

202 distinct IDs. Per group (fxid min..max):

| gid | Category | IDs | fxid range |
|---|---|---|---|
| 1 | DYN | 14 | 8..160 |
| 2 | FLT | 24 | 8..256 |
| 3 | DRV | 21 | 8..320 |
| 4 | AMP | 8 | 8..320 |
| 5 | CAB | 12 | 16..192 |
| 6 | MOD | 39 | 8..512 |
| 7 | SFX | 13 | 16..208 |
| 8 | DLY | 26 | 8..384 |
| 9 | REV | 29 | 8..448 |
| 12 | ? | 8 | 16..192 |
| 13 | ? | 6 | 16..96 |
| 15 | ? | 2 | 0..16 |

The highest stock fxid is 512. Zoom never uses 513 or above in these updaters.

## Why 900-1023

The patch stores `((fxid&0x3F)<<17) | (((fxid>>6)&1)<<30) | (((fxid>>7)&7)<<8) | (gid<<1)`,
so fxid has 10 bits: 1023 is the top. Above that, IDs alias lower ones. 900-1023 leaves a gap above
Zoom's 512 for future stock effects and fits 124 pack effects per group.

## Proposed pack IDs (not used yet)

| fxid | Effect | gid |
|---|---|---|
| 900 | LOUDER | 1 DYN |
| 901 | Gooey | 1 DYN |
| 902 | DJFilter | 1 DYN |
| 903 | EuGate | 1 DYN |
| 904 | Breather | 1 DYN |
| 905 | DirtBox | 3 DRV |
| 906 | Wavefold | 3 DRV |
| 907 | Takeoff | 6 MOD |
| 908 | Choral (formant) | 6 MOD |
| 909 | SGNL | 6 MOD |
| 910 | DualShft | 6 MOD |
| 911 | Sweep | 6 MOD |
| 912 | GRDLOOP (looper) | 8 DLY |
| 913 | GRDELAY | 8 DLY |
| 914 | DubSiren | 8 DLY |
| 915 | Scrub | 8 DLY |
| 916 | Washout | 9 REV |

If this is ever done: saved patches store the ID, so patches saved with the old numbers lose these effects.

## Not checked

- Pedal: loading, patch save/recall and the effect list with IDs 900+ (no pedal here).
- Effect Manager: its efx_1_30.dat is an encrypted zip, so its ID list can't be read.
- Editors (ToneLib, zoom-ms-utility, others): they keep their own ID tables and may show pack
  effects as unknown, as with any non-stock ID.
- Other updaters (MS-50G+, MS-70CDR+, G1on/B1on, Effect Manager packs): not on this PC.
- Groups 12, 13, 15: not named yet.
