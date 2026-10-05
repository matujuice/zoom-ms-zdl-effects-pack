# Keeping the effects on the bar from an iPhone

The pedal ignores MIDI clock, so an LFO, pattern or grain slowly drifts away from your drum machine. The Mozaic script `tools/mozaic/zoom_bar_sync.txt` fixes that: it restarts DualShft, Choral, EuGate, DubSiren and Scrub on every downbeat.

**How it works.** Every Tempo knob holds each BPM twice (40..240, then 40..240 again). Jumping from a BPM to its twin copy restarts the effect without changing the tempo. On each downbeat the script flips the Tempo knob of the effects in slots 1 to 3 to its other copy, over USB SysEx. It sends the edit a little early (Early, default 17 ms) so it lands on the beat. When the host tempo changes, it sends the new BPM to the same knobs.

**DubSiren:** set Trig to SHold or SPuls. Then a press of the footswitch waits for the next beat, and the siren's tones stay in step with the bar. With Hold or Pulse the siren starts the moment you press, and the press restarts its LFO.

**Tested** on an MS-60B running MS-50G firmware, iPhone with AUM and Mozaic, Digitakt mk1 following AUM over USB (2026-10-05). Measured edit latency about 12 ms.

## Setup

1. Connect the pedal and the drum machine to the iPhone through a powered USB hub on the camera adapter.
2. AUM is the master clock. In AUM's clock settings, send MIDI clock to the drum machine. On a Digitakt: SETTINGS > MIDI CONFIG > SYNC, Clock receive and Transport receive on; PORT CONFIG, Input from USB.
3. Add Mozaic to an AUM channel, paste `zoom_bar_sync.txt` into its code view and tap Upload.
4. In AUM's MIDI routing: Mozaic out to the Zoom.
5. Tap the first three pads until each shows the effect in that slot (Slot 1: EuGate, and so on). Only the first three effects of a patch accept edits from outside.
6. Press play in AUM.

## Controls

| Control | What it does |
|---|---|
| Early (knob 1) | How many ms before the downbeat the edit is sent. Raise it if restarts land late. |
| Every (knob 2) | Restart every 1, 2, 4 or 8 bars. |
| BPM (knob 3) | Tempo to send when Follow host is off. |
| Follow host (knob 4) | Right half: the effects follow AUM's tempo. Left half: they use the BPM knob. |
| Slot 1..3 (pads 1..3) | Which effect is in each slot. |
| Sync (pad 4) | Restarts on or off. |

## Measuring Early

`tools/mozaic/zoom_tempo_test.txt` checks the connection (Sweep moves knob 1 of slot 1) and has a latency test. The easier way is bar sync itself: EuGate in slot 1 with Notes 1, Steps 16, Soft 0, Mix 100 lets one 16th per bar through. Raise Early from 0 until that kick keeps its click, then add about 5 ms.

## Notes from testing

- The patch-tempo SysEx (`31 03 08`) did nothing on the MS-60B, so the script sets each effect's Tempo knob instead.
- The pedal did not answer the identity request; ID 58 works for the MS-60B on MS-50G firmware.
- Mozaic shows only 4 pads, wants every variable set in `@OnLoad`, and rejects expressions such as `Round (x)` inside `Log`.
