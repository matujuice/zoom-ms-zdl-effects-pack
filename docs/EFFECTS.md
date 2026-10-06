# The effects

Every knob shows its real value on the pedal's screen where that makes sense (BPM, milliseconds, note names).

**Tempo knobs hold every BPM twice.** On DualShft, Choral, EuGate, DubSiren and Scrub the Tempo knob runs 40..240 BPM and then, past 240, shows 40..240 again (a twin copy). Both copies play the same tempo. Jumping from a BPM to its twin (120 to the second 120) restarts the LFO, pattern or grain right away without changing the tempo. That is meant for a MIDI host (an iPhone script or a small box) that sends this one knob edit on every downbeat to keep the effect on the bar. By hand, just stay on one copy. Tempo is the 8th knob on all five, so the host sends the same knob number whatever the effect. While one of these effects is switched off its LFO or pattern keeps running in the background, so it comes back in time (DubSiren needs Trig SHold or SPuls for that, EuGate Reset OFF, NOTE or SYNC).

### WaveFold: wavefolder
A fold curve from a published Buchla 259 model (Esqueda, Pontynen, Valimaki and Parker, DAFx-2017). Five parallel folding stages bend the sound back on itself as Drive goes up. The loudness is matched to your input automatically. No oversampling, so high notes at high Drive alias.

| Knob | Range | What it does |
|---|---|---|
| Drive | 0..100 | How hard the signal is pushed into the folder. Higher = more folds. |
| Symm | -50..+50 | 0 is symmetric. Away from 0 adds even harmonics. |
| Tone | 0..100 | Low-pass after the fold, about 200 Hz up to open. |
| Level | 0..100 | 50 = wet matches the input loudness, 100 = +6 dB. |
| Mix | 0..100 | Dry/wet crossfade, DJ style: the dry sound stays full up to 50, the effect is full from 50, so at 50 both play at full level. |

### DualShft: tempo-synced dual pitch shifter
Two pitch-shift voices, each with its own echo time, plus one tempo-synced LFO that bends them in opposite directions. No feedback.

| Knob | Range | What it does |
|---|---|---|
| Ptch1 / Ptch2 | -24..+24 semitones | Pitch of each voice, with tenths of a semitone around 0. |
| Dly1 / Dly2 | 12 ms..1 s, note values | Echo time of each voice. The top of the knob syncs to Tempo as note values (1/32 .. 1 bar). A synced time too long for the 3.9 s buffer is halved (1 bar below about 62 BPM). |
| Div | 4 bars .. 1/64 | Length of one LFO cycle (dotted and triplet values included). |
| Depth | 0..12 semitones | How far the LFO bends the pitch. 0 = no LFO. |
| Shape | Tri, Sqr, Rand, Step, Sine, Rise, Fall | LFO shape. |
| Tempo | 40..240 BPM, twice | Tempo for the LFO. The LFO restarts when you change it. The second copy restarts it on the same tempo (see above). |
| Mix | 0..100 | Dry/wet crossfade, DJ style: the dry sound stays full up to 50, the effect is full from 50, so at 50 both play at full level. |

### Choral: vowel choir
Filters tuned to the vowels A E I O U turn the input into a small choir. Five voices, a main voice with a human touch (vibrato, drift, breath) and four side voices.

| Knob | Range | What it does |
|---|---|---|
| Vowel | A..U | Morphs between the five vowels. |
| Reso | 0..100 | How sharp the vowel is. Higher = more vocal. |
| Chord | OFF, detune, 2..7 semitones, 35 chords | The pitch of the side voices relative to the main voice. |
| Param | 0..100 | Changes meaning with Shape (lag, glide, independence, chance, speed ...). |
| Div | 4 bars .. 1/64 | Length of one LFO cycle. |
| Shape | Sine, Step, Rand, Solo, Some, Canon, Ripl, Fan, Walk, Swell, Spot | How the vowel moves between the voices. |
| Depth | 0..100 | How far the LFO reaches from the Vowel setting. 0 = the shape does nothing. |
| Tempo | 40..240 BPM, twice | Tempo for the vowel LFO. The LFO restarts when you change it. The second copy restarts it on the same tempo (see above). |
| Mix | 0..100 | Dry/wet crossfade, DJ style: the dry sound stays full up to 50, the effect is full from 50, so at 50 both play at full level. |

### EuGate: Euclidean rhythm gate
Chops the sound into a repeating rhythm. Every step is a 16th note. Notes are spread as evenly as possible over the steps (a Euclidean rhythm).

| Knob | Range | What it does |
|---|---|---|
| Notes | 1..64 | How many notes play. At or above Steps = every step. |
| Steps | 1..64 | Pattern length. Anything other than 16, 32 or 64 runs against the bar: polymeters. |
| Shift | 0..63 | Starts the pattern later by this many steps. |
| Swing | 0..100 | Delays only the weak 16ths. |
| Reset | OFF, NOTE, PEDAL, SYNC | What restarts the pattern: nothing, every new note after silence, or turning the effect on. SYNC is the setting for bar sync from a host: the same as OFF, only the host's restarts count. |
| Gap | 0..50 % of a step | Small silence at the end of a note that is followed by another note. |
| Soft | 0..100 | Softness of the note edges. 0 = hard chop. |
| Tempo | 40..240 BPM, twice | Tempo. Jumping to the second copy of the same BPM restarts the pattern at step 1 (see above). |
| Mix | 0..100 | Dry/wet crossfade, DJ style: the dry sound stays full up to 50, the effect is full from 50, so at 50 both play at full level. The gaps are silent in the wet sound. |

Try 5 notes in 16 steps (the default), then 7 in 12 or 5 in 12 for a different feel.

### DubSiren: dub siren with tape echo
A siren oscillator with LFO modes, played from the footswitch, into its own tape-style echo. Your input passes through untouched.

| Knob | Range | What it does |
|---|---|---|
| Trig | Hold / Pulse / SHold / SPuls | Hold = sounds while the effect is on. Pulse = one short burst each time you turn it on. SHold and SPuls do the same on the beat: the siren waits for the next beat of the Tempo clock (re-aligned by each twin flip from a host) and the siren tones stay in step with the bar. |
| Mode | Wail, Fast, Slow, Laser | LFO shape. Fast is twice the Rate, Slow is half. |
| Pitch | 110 Hz..1760 Hz | Base pitch. |
| Rate | Man, 0.15..15 Hz, note values | LFO speed. The top of the knob syncs to Tempo as note values (4 bars .. 1/32). |
| Depth | 0..100 | How far the LFO sweeps the pitch. |
| Vol | 0..100 | Siren level. |
| Time | 50 ms..1 s | Echo time (never synced). |
| Tempo | 40..240 BPM, twice | Used when Rate is set to a note value and by SHold / SPuls. Jumping to the second copy of the same BPM restarts the LFO (see above). |
| Fdbk | 0..125 | Echo repeats. 0 = no echo, above 100 it self-oscillates. |

### S.GN_L: broken digital line
The sound is cut into packets and some of them never arrive, like a VoIP call on bad Wi-Fi or a digital radio losing lock. What fills the hole is most of the character: silence, the last packet replayed as a buzz or a stutter, a fading replay, or hiss. A Codec knob wrecks the quality on top (spectral holes, a closing low-pass, a lower sample rate, fewer bits). The file is `SGNL.ZDL`; the pedal shows the name S.GN_L.

| Knob | Range | What it does |
|---|---|---|
| Loss | 0..100 | How many packets are lost. 0 = clean line, 100 = about 85 %. |
| Size | 1 ms..500 ms | Packet length. Short = grit and, with REPT, a buzz at the packet rate (5 ms = about 200 Hz). Long = notes and words drop out or stutter. |
| Codec | 0..100 | Codec quality going down. Above 50 the first packet after a silence is lost too, like a call clipping the start of a word. |
| Fill | GAP, REPT, FADE, NOISE, REVRS, GARBL, LATE, RND | What replaces a lost packet: silence, the last packet replayed, the replay dying away, hiss at the level of the sound, the last packet backwards, corrupted data (boosted random slices, a digital screech), an older packet out of order so phrases shuffle, or a random one of these per outage (never GARBL). |
| Burst | 0..100 | 0 = scattered single losses, 100 = long outages, with the same overall amount. |
| Jump | 0..100 | How often the quality suddenly drops for a few packets and comes back. |
| Line | HIFI, VOIP, PHONE, WALKY | Band of the line: full and untouched, 200 Hz..5 kHz with a steep low cut and a boxy headset bump at 1.5 kHz, 300 Hz..3.4 kHz, or 500 Hz..2.5 kHz with some drive. |
| Edge | 0..100 | Cut at the packet edges. 0 = hard clicks, 100 = fades that fill most of the packet, so lost packets become soft dips and swells (longer with bigger Size). |
| Mix | 0..100 | Dry/wet crossfade, DJ style: dry full up to 50, wet full from 50, both full at 50. |

Try Size 5 ms with Fill REPT and Burst high for robot voice, or Size 60 ms, Fill GAP and Line PHONE for a bad call.

### Scrub: scrub through the past, freeze where you stop
Everything you play is recorded into a 7.9 second buffer. Pos moves a read head through it: turning it, short grains follow the head through the audio at the original pitch, and when you stop, the grain just before the head loops on and on, crossfaded so the loop has no click. Known from Mutable Instruments Clouds/Beads (Position + Freeze) and the Morphagene.

| Knob | Range | What it does |
|---|---|---|
| Pos | 6.00 s..0 ms back | Where the head is in the last 6 seconds, in 10 ms steps (600 steps). The screen shows how far back it is; 0ms is the present (in HOLD, the moment you froze). |
| Grain | 10 ms..1 s, then 1/64..1/2 | Length of the loop. Short = a buzzing tone made of the sound, long = a whole hit or chord repeating. Past 1 s come note values synced to Tempo (1/64, 1/32, 1/16T, 1/16, 1/8T, 1/16., 1/8, 1/4T, 1/8., 1/4, 1/4., 1/2), so the freeze loops in time with the song. Long grains pull the head in so the whole grain still fits in the buffer: a synced grain over about 1 s, or with Dir REV, PING or RAND in LIVE any grain over about 0.6 s (a backward grain in LIVE reads further back, because the recording moves on under it). A synced grain too long to fit even at 0ms (1/2 below about 55 BPM, backward in LIVE) is shortened. |
| Rec | LIVE, HOLD, STOMP | LIVE keeps recording, so Pos is how far back you listen: a delay you can sweep. HOLD stops recording and Pos scrubs the last 6 of the 7.9 seconds kept. STOMP records while the effect is switched off and freezes the moment you switch it on (needs the pedal to pass the sound through a switched-off effect; if ON gives silence, use LIVE and HOLD). |
| Glide | 0..100 | How slowly the head follows Pos. 0 = it jumps, 40 = about 0.1 s, 100 = about 3 s, a slow glide. It also hides the knob's steps. |
| Dir | FWD, REV, PING, RAND | Which way the grain plays: forward, backward, ping-pong (forward then backward in turns) or random (each grain flips a coin). PING turns round on itself, so the loop has no jump and sounds smoother and more tonal. |
| Spray | OFF, +-1..+-250 | Random offset for each new grain, either side of the head, shown in ms (+-250 = up to 250 ms before or after; never ahead of now): a moving cloud instead of a steady loop. |
| Mix | 0..100 | Dry/wet crossfade, DJ style: dry full up to 50, wet full from 50, both full at 50. |
| Tempo | 40..240 BPM, twice | Only for the synced Grain values. The pedal gives custom effects no clock, so dial in your song's tempo, as on DubSiren. Jumping to the second copy of the same BPM starts a new grain at once (see above). |

Try: play a Digitakt loop in LIVE, turn Rec to HOLD, then sweep Pos slowly with Glide around 60 and stop on a snare. Grain 10..30 ms on a held synth chord gives a buzzy drone; Spray 30 makes it a cloud.

### DIRTBOX: a distortion box for an acid machine (new, not in a release yet)
Made for a TB-303 / TD-3 (or a MeeBlip triode) going into the pedal: seven real distortion pedals on the Model knob, mildest first, a 3-band EQ voiced for acid, and an automatic noise reducer (ZNR). It was DirtBox until 2026-10-06 (same fxid 491). Drive mostly adds dirt rather than volume: an input peaking around -14 dBFS comes out at about the same level at any Drive.

Each model follows the real circuit stage by stage, from a published analysis or a circuit-level digital model (filters evaluated at 44.1 kHz, diode and transistor curves fitted to the model's):

- **TS9**: Ibanez TS9 Tube Screamer, after guitarix's [ts9sim](https://github.com/brummer10/guitarix/blob/master/trunk/src/plugins/ts9sim.dsp): gain only above 720 Hz, the 1N914s soft-clipping in the feedback, the 723 Hz low-pass with the treble control. The mid-hump overdrive.
- **DIST+**: MXR Distortion+, after the ElectroSmash analysis and guitarix's [mxrdist](https://github.com/brummer10/guitarix/blob/master/trunk/src/LV2/faust/mxrdist.dsp): 741 gain up to 214x, the 741's limited bandwidth, germanium diodes. The pedal has no tone knob, so Tone is a gentle low-pass.
- **DS-1**: the distortion built into the Behringer TD-3, a copy of the Boss DS-1 ([ElectroSmash](https://www.electrosmash.com/boss-ds1-analysis), [DS1.lv2](https://github.com/LiamLombard/DS1.lv2)): 35 dB booster, op-amp stage, 1N4148s, the DS-1 tone.
- **RAT2**: ProCo RAT 2 (same gain stage and clipper as the RAT), after the [Proco-Rat nodal model](https://github.com/Rudro085/Proco-Rat): two gain legs, the LM308's treble loss as gain rises, 1N914s, the Filter.
- **MUFF**: Big Muff Pi, guitarix's [bmp](https://github.com/brummer10/guitarix/blob/master/trunk/src/LV2/faust/bmp.dsp) circuit model: two transistor clipping stages and the Big Muff tone stack (noon = the mid scoop). Drive = Sustain. The Muff's input booster (about +24 dB), which bmp leaves out, is added back, so it saturates at synth levels like the real pedal.
- **SFUZZ**: Univox Super-Fuzz, from the schematic (no circuit-level digital model was found): high-gain preamp, the push-push doubler (octave up), germanium diodes. Tone = the Tone switch made continuous: 0 = the 1 kHz scoop fully in (fat and bassy), 100 = off. Drive = Expander.
- **MT-2**: Boss MT-2 Metal Zone, guitarix's [MetalTone](https://github.com/brummer10/MetalTone) circuit model: +25 dB mid pre-filter, Dist stage, the clipper, the post filter. Tone = the High knob.

Only RAT2 is nearly clean at Drive 0; the others still distort at 0, as the real pedals do. No oversampling, so high notes at full Drive alias a little.

| Knob | Range | What it does |
|---|---|---|
| Model | TS9, DIST+, DS-1, RAT2, MUFF, SFUZZ, MT-2 | Which pedal, mildest first. Default RAT2. Switching fades the effect back in over a few ms so it doesn't click. |
| Drive | 0..100 | The pedal's own gain knob (Drive, Distortion, Dist, Sustain, Expander). |
| Tone | 0..100 | The pedal's tone control, 100 = brightest. Default 80. |
| Low | -12..+12 dB | Shelf at 110 Hz: the 303's body. 0 = flat (default). |
| Mid | -12..+12 dB | Peak at 1 kHz: the squelch of the resonance. 0 = flat (default). |
| High | -12..+12 dB | Shelf at 4.5 kHz: the fizz. 0 = flat (default). |
| ZNR | 0..100 | Noise reducer on the distorted sound only. It measures your noise floor by itself and stays shut this far above it: +6 dB at 1, +15 dB at 50, +24 dB at 100. 0 = off. |
| Level | 0..100 | Distorted level, 50 = about the input level, 100 = +6 dB. |
| Mix | 0..100 | Dry/wet crossfade, DJ style: dry full up to 50, wet full from 50, both full at 50 (parallel distortion). |

**ZNR for acid lines.** It listens to the clean input, not the distorted sound, so it follows each note's real decay. The noise floor it measures can never be set above -60 dBFS, so the threshold never climbs into the music. Below the threshold it waits 30 ms, then turns the distortion down gently (a 1:3 expander) over about 80 ms: quick enough to clear the hiss in a rest between 16ths at 140 BPM, while a note's tail still fades the way it does on the input. More gain means more hiss to clear; turning ZNR up cuts more.

Try: TD-3 into RAT2 at Drive 50, Tone 80, Mid +3 for a classic squelch; DS-1 for the TD-3's own distortion sound; SFUZZ with Tone 0 and Low +4 for fat, octave-up drones; Mix 50 to keep the clean line under the dirt.
