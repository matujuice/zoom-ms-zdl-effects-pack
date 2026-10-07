# Keeping the effects on the bar from an iPhone

The pedal ignores MIDI clock, so an LFO, pattern or grain slowly drifts away from your drum machine. The Mozaic script **Zoom MS bar sync** fixes that: it restarts DualShft, Choral, EuGate, DubSiren and Scrub on every downbeat. Get it from [PatchStorage](https://patchstorage.com/zoom-ms-bar-sync-keep-custom-zoom-effects-on-the-beat/) or from this repo ([zoom_bar_sync.mozaic](../tools/mozaic/zoom_bar_sync.mozaic); plain text in [zoom_bar_sync.txt](../tools/mozaic/zoom_bar_sync.txt)).

**How it works.** Every Tempo knob holds each BPM twice (40..240, then 40..240 again). Jumping from a BPM to its twin copy restarts the effect without changing the tempo. On each downbeat the script flips the Tempo knob of the effects in slots 1 to 3 to its other copy, over USB SysEx. It sends the edit a little early (Early, default 17 ms) so it lands on the beat. When the host tempo changes, it sends the new BPM to the same knobs.

## Setting the effects for bar sync

Some settings restart an effect on their own (a press, a note), which pulls it off the bar until the next flip. Use these:

| Effect | Set | Avoid |
|---|---|---|
| DualShft | Any Div. Dly1 / Dly2 on note values (the top of the knob) to put the echoes on the grid. | Turning Tempo by hand: it restarts the LFO. |
| Choral | Any Div. | Turning Tempo by hand: it restarts the LFO. |
| EuGate | Reset SYNC (works like OFF). | NOTE (restarts on a note after silence) and PEDAL (restarts when you press). |
| DubSiren | Trig SHold or SPuls: the siren waits for the next beat. Rate on a note value (the top of the knob) keeps its tones on the bar. | Hold and Pulse: they start at the press and restart the LFO. |
| Scrub | Grain on a note value (past 1 s on the knob). Rec LIVE or HOLD. | Switching it on starts a new grain at once, so it is back on the bar at the next flip. STOMP freezes at the press. |
| GridDly | Time on a note value (the top of the knob). REVRS chunks restart on each bar. In development, not tested on the pedal. | Turning Tempo by hand on REVRS: it restarts the chunk. |
| SyncEQ | Put it in slots 1 to 3 with that slot's pad ON. It passes the bar on to later slots, also while switched off. In development, not tested on the pedal. | |

**Slots 4 to 6 (in development, not tested on the pedal yet).** A tempo effect that the script flips in slots 1 to 3 (or SyncEQ) passes each bar on to the effects after it. Effects with Tempo all the way down on FOLLW follow it, bars and BPM; on a BPM they run on their own. Turn ON one pad only, for the one tempo effect or SyncEQ that sends, and put every other synced effect on FOLLW (two pads on still work, one of them sends, but there is no reason for it); an effect in slots 1 to 3 on FOLLW needs its pad OFF, or the script sets its Tempo to a BPM.

WaveFold and S.GN_L have no tempo, so nothing to set. Leave Tempo to the script (Follow host on), and use Every 1 so a missed restart is fixed within a bar. DualShft, Choral, EuGate and DubSiren keep their clock running while switched off, so they come back on the bar when you switch them on.

**Tested** (script v1) on an MS-60B running MS-50G firmware, iPhone with AUM and Mozaic, Digitakt mk1 following AUM over USB (2026-10-05). Measured edit latency about 12 ms. Script v2 (slot on/off pads, Tempo always the 8th knob) needs the effects with Tempo on the 8th knob (release v1.4 and later); it works on the pedal too (Luca, 2026-10-05).

## Setup

1. Connect the pedal and the drum machine to the iPhone through a powered USB hub on the camera adapter.
2. AUM is the master clock. In AUM's clock settings, send MIDI clock to the drum machine. On a Digitakt: SETTINGS > MIDI CONFIG > SYNC, Clock receive and Transport receive on; PORT CONFIG, Input from USB.
3. On the iPhone, download `zoom_bar_sync.mozaic` from [PatchStorage](https://patchstorage.com/zoom-ms-bar-sync-keep-custom-zoom-effects-on-the-beat/) (Download button) or from [tools/mozaic/](../tools/mozaic/zoom_bar_sync.mozaic) into Files. Use v2 (pads say "Slot 1 ON"); v1 asked which effect is in each slot and needs the old knob layout.
4. Open the file in Files and share it to Mozaic. Add Mozaic to an AUM channel and pick the bar sync preset in its Presets tab. It is ready to run: no code to paste.
   No .mozaic? Open [zoom_bar_sync.txt](../tools/mozaic/zoom_bar_sync.txt), copy all of it, paste it into Mozaic's code view and tap Upload.
5. In AUM's MIDI routing: Mozaic out to the Zoom.
6. The first three pads switch sync on or off for slots 1 to 3 (all ON at load). Tempo is the 8th knob on all five synced effects, so the script doesn't need to know which one is where. Turn a slot OFF if it holds any other effect, or the script will move that effect's 8th knob. Only the first three effects of a patch accept edits from outside.
7. Press play in AUM.

## Controls

| Control | What it does |
|---|---|
| Early (knob 1) | How many ms before the downbeat the edit is sent. Raise it if restarts land late. |
| Every (knob 2) | Restart every 1, 2, 4 or 8 bars. |
| BPM (knob 3) | Tempo to send when Follow host is off. |
| Follow host (knob 4) | Right half: the effects follow AUM's tempo. Left half: they use the BPM knob. |
| Slot 1..3 (pads 1..3) | Sync on or off for each slot. Off for any slot without DualShft, Choral, EuGate, DubSiren or Scrub. |
| Sync (pad 4) | Restarts on or off. |

## Measuring Early

`tools/mozaic/zoom_tempo_test.txt` checks the connection (Sweep moves knob 1 of slot 1) and has a latency test. The easier way is bar sync itself: EuGate in slot 1 with Notes 1, Steps 16, Soft 0, Mix 100 lets one 16th per bar through. Raise Early from 0 until that kick keeps its click, then add about 5 ms.

## Notes from testing

- The patch-tempo SysEx (`31 03 08`) did nothing on the MS-60B, so the script sets each effect's Tempo knob instead.
- The pedal did not answer the identity request; ID 58 works for the MS-60B on MS-50G firmware.
- Mozaic shows only 4 pads, wants every variable set in `@OnLoad`, and rejects expressions such as `Round (x)` inside `Log`.
- `zoom_bar_sync.mozaic` is Luca's v1 export from Mozaic with the v2 text put in its code field (and the pad labels updated). After changing the text, upload it in Mozaic, save, export the new .mozaic and update PatchStorage too.
