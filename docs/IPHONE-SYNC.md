# Keeping the effects on the bar from an iPhone

The pedal ignores MIDI clock, so an LFO, pattern or grain slowly drifts away from your drum machine. The Mozaic script `tools/mozaic/zoom_bar_sync.txt` fixes that: it restarts DualShft, Choral, EuGate, DubSiren and Scrub on every downbeat.

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

WaveFold and S.GN_L have no tempo, so nothing to set. Leave Tempo to the script (Follow host on), and use Every 1 so a missed restart is fixed within a bar. DualShft, Choral, EuGate and DubSiren keep their clock running while switched off, so they come back on the bar when you switch them on.

**Tested** on an MS-60B running MS-50G firmware, iPhone with AUM and Mozaic, Digitakt mk1 following AUM over USB (2026-10-05). Measured edit latency about 12 ms.

## Setup

1. Connect the pedal and the drum machine to the iPhone through a powered USB hub on the camera adapter.
2. AUM is the master clock. In AUM's clock settings, send MIDI clock to the drum machine. On a Digitakt: SETTINGS > MIDI CONFIG > SYNC, Clock receive and Transport receive on; PORT CONFIG, Input from USB.
3. Add Mozaic to an AUM channel, paste `zoom_bar_sync.txt` into its code view and tap Upload.
4. In AUM's MIDI routing: Mozaic out to the Zoom.
5. The first three pads switch sync on or off for slots 1 to 3 (all ON at load). Tempo is the 8th knob on all five synced effects, so the script doesn't need to know which one is where. Turn a slot OFF if it holds any other effect, or the script will move that effect's 8th knob. Only the first three effects of a patch accept edits from outside.
6. Press play in AUM.

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
