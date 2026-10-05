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

### DirtBox: three distortions and a noise reducer (new, not in a release yet)
One distortion with three characters on the Model knob, and an automatic noise reducer (ZNR) built in. Drive mostly adds dirt rather than volume: the output is levelled so that an input peaking around -14 dBFS comes out at about the same peak at any Drive.

- **ACID**: the distortion built into the Behringer TD-3 (the 303 clone; the original TB-303 has none). The TD-3's is a copy of the Boss DS-1: a transistor booster, an op-amp stage that boosts only above 72 Hz, hard silicon-diode clipping, and the DS-1 tone. Made for acid lines and drones from a MeeBlip triode or similar.
- **RAT**: after the ProCo RAT. Only the sound above about 70 Hz gets the full gain, the top end softens as Drive goes up (as the RAT's op-amp does), and hard diode clipping follows. Tone is the RAT's Filter, turned round: up = brighter.
- **METAL**: after the Boss Metal Zone. Two clipping stages for a lot of gain, the lows under 100 Hz are kept out of it so it stays tight, and the mids are scooped (-9 dB at 750 Hz).

These are sketches of each pedal's character, not component-level models. No oversampling, so high notes at full Drive alias a little.

| Knob | Range | What it does |
|---|---|---|
| Model | ACID, RAT, METAL | Which distortion. Switching fades the effect back in over a few ms so it doesn't click. |
| Drive | 0..100 | How hard the signal hits the clipper: 0.5x at 0 up to 120x (ACID), 300x (RAT) or 60x into a second 6x stage (METAL). |
| Tone | 0..100 | 100 = brightest on all three. ACID: the DS-1 tone, blending a 234 Hz low-pass (0) with a 1.06 kHz high-pass (100); the middle scoops the mids. RAT: low-pass 475 Hz..16 kHz. METAL: low-pass 1.2..12 kHz. |
| ZNR | 0..100 | Noise reducer on the distorted sound only (the dry sound is never touched). It measures your noise floor by itself and stays shut this far above it: +6 dB at 1, +15 dB at 50, +24 dB at 100. 0 = off. |
| Level | 0..100 | Distorted level, 50 = about the input level, 100 = +6 dB. |
| Mix | 0..100 | Dry/wet crossfade, DJ style: dry full up to 50, wet full from 50, both full at 50. At 50 you get parallel distortion: the clean kick stays under the dirt. |

**How ZNR keeps kick tails.** It listens to the clean input, not the distorted sound, so it follows the kick's real decay. The noise floor it measures can never be set above -60 dBFS, so in a track that never goes quiet the threshold stays at -45 dBFS or lower (ZNR 50) and never climbs into the music. Below the threshold it does not cut: it waits 50 ms, then turns the distortion down gently (a 1:3 expander) over about 150 ms, so a tail fades the way it fades on the input. In the host test a 160 BPM kick pattern came through unchanged while the noise between songs dropped by about 30 dB.

Try: Digitakt kick and bass at Drive 60, Mix 50 for parallel grit; RAT with Tone 30 for a fat, dark fuzz on a Shruthi bass; METAL on hats and snares only.
