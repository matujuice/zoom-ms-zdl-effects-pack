# The seven effects

Every knob shows its real value on the pedal's screen where that makes sense (BPM, milliseconds, note names).

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
| Dly1 / Dly2 | 12 ms..1 s | Echo time of each voice (free, not synced). |
| Tempo | 40..240 BPM | Tempo for the LFO. |
| Div | 4 bars .. 1/64 | Length of one LFO cycle (dotted and triplet values included). |
| Depth | 0..12 semitones | How far the LFO bends the pitch. 0 = no LFO. |
| Shape | Tri, Sqr, Rand, Step, Sine, Rise, Fall | LFO shape. |
| Mix | 0..100 | Dry/wet crossfade, DJ style: the dry sound stays full up to 50, the effect is full from 50, so at 50 both play at full level. |

### Choral: vowel choir
Filters tuned to the vowels A E I O U turn the input into a small choir. Five voices, a main voice with a human touch (vibrato, drift, breath) and four side voices.

| Knob | Range | What it does |
|---|---|---|
| Vowel | A..U | Morphs between the five vowels. |
| Reso | 0..100 | How sharp the vowel is. Higher = more vocal. |
| Chord | OFF, detune, 2..7 semitones, 35 chords | The pitch of the side voices relative to the main voice. |
| Param | 0..100 | Changes meaning with Shape (lag, glide, independence, chance, speed ...). |
| Tempo | 40..240 BPM | Tempo for the vowel LFO. |
| Div | 4 bars .. 1/64 | Length of one LFO cycle. |
| Shape | Sine, Step, Rand, Solo, Some, Canon, Ripl, Fan, Walk, Swell, Spot | How the vowel moves between the voices. |
| Depth | 0..100 | How far the LFO reaches from the Vowel setting. 0 = the shape does nothing. |
| Mix | 0..100 | Dry/wet crossfade, DJ style: the dry sound stays full up to 50, the effect is full from 50, so at 50 both play at full level. |

### EuGate: Euclidean rhythm gate
Chops the sound into a repeating rhythm. Every step is a 16th note. Notes are spread as evenly as possible over the steps (a Euclidean rhythm).

| Knob | Range | What it does |
|---|---|---|
| Notes | 1..64 | How many notes play. At or above Steps = every step. |
| Steps | 1..64 | Pattern length. Anything other than 16, 32 or 64 runs against the bar: polymeters. |
| Shift | 0..63 | Starts the pattern later by this many steps. |
| Swing | 0..100 | Delays only the weak 16ths. |
| Reset | OFF, NOTE, PEDAL | What restarts the pattern: nothing, every new note after silence, or turning the effect on. |
| Gap | 0..50 % of a step | Small silence at the end of a note that is followed by another note. |
| Soft | 0..100 | Softness of the note edges. 0 = hard chop. |
| Tempo | 40..240 BPM | Tempo. |
| Mix | 0..100 | Dry/wet crossfade, DJ style: the dry sound stays full up to 50, the effect is full from 50, so at 50 both play at full level. The gaps are silent in the wet sound. |

Try 5 notes in 16 steps (the default), then 7 in 12 or 5 in 12 for a different feel.

### DubSiren: dub siren with tape echo
A siren oscillator with LFO modes, played from the footswitch, into its own tape-style echo. Your input passes through untouched.

| Knob | Range | What it does |
|---|---|---|
| Trig | Hold / Pulse | Hold = sounds while the effect is on. Pulse = one short burst each time you turn it on. |
| Mode | Wail, Fast, Slow, Laser | LFO shape. Fast is twice the Rate, Slow is half. |
| Pitch | 110 Hz..1760 Hz | Base pitch. |
| Rate | Man, 0.15..15 Hz, note values | LFO speed. The top of the knob syncs to Tempo as note values (4 bars .. 1/32). |
| Depth | 0..100 | How far the LFO sweeps the pitch. |
| Vol | 0..100 | Siren level. |
| Time | 50 ms..1 s | Echo time (never synced). |
| Fdbk | 0..125 | Echo repeats. 0 = no echo, above 100 it self-oscillates. |
| Tempo | 40..240 BPM | Only used when Rate is set to a note value. |

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
Everything you play is recorded into a 6 second buffer. Pos moves a read head through it: turning it, short grains follow the head through the audio at the original pitch, and when you stop, the grain just before the head loops on and on, crossfaded so the loop has no click. Known from Mutable Instruments Clouds/Beads (Position + Freeze) and the Morphagene.

| Knob | Range | What it does |
|---|---|---|
| Pos | 4.00 s..0 ms back | Where the head is in the last 4 seconds, in 10 ms steps (400 steps). The screen shows how far back it is; 0ms is the present (in HOLD, the moment you froze). |
| Grain | 10 ms..1 s, then 1/64..1/2 | Length of the loop. Short = a buzzing tone made of the sound, long = a whole hit or chord repeating. Past 1 s come note values synced to Tempo (1/64, 1/32, 1/16T, 1/16, 1/8T, 1/16., 1/8, 1/4T, 1/8., 1/4, 1/4., 1/2), so the freeze loops in time with the song. Long grains pull the head in so the whole grain still fits in the 6 s: a synced grain over about 1 s, or with Dir REV, PING or RAND in LIVE any grain over about 0.6 s (a backward grain in LIVE reads further back, because the recording moves on under it). A synced grain too long to fit even at 0ms (1/2 below about 55 BPM, backward in LIVE) is shortened. |
| Rec | LIVE, HOLD, STOMP | LIVE keeps recording, so Pos is how far back you listen: a delay you can sweep. HOLD stops recording and Pos scrubs the last 6 seconds. STOMP records while the effect is switched off and freezes the moment you switch it on (needs the pedal to pass the sound through a switched-off effect; if ON gives silence, use LIVE and HOLD). |
| Glide | 0..100 | How slowly the head follows Pos. 0 = it jumps, 40 = about 0.1 s, 100 = about 3 s, a slow glide. It also hides the knob's steps. |
| Dir | FWD, REV, PING, RAND | Which way the grain plays: forward, backward, ping-pong (forward then backward in turns) or random (each grain flips a coin). PING turns round on itself, so the loop has no jump and sounds smoother and more tonal. |
| Spray | OFF, +-1..+-250 | Random offset for each new grain, either side of the head, shown in ms (+-250 = up to 250 ms before or after; never ahead of now): a moving cloud instead of a steady loop. |
| Mix | 0..100 | Dry/wet crossfade, DJ style: dry full up to 50, wet full from 50, both full at 50. |
| Tempo | 40..240 BPM | Only for the synced Grain values. The pedal gives custom effects no clock, so dial in your song's tempo, as on DubSiren. |

Try: play a Digitakt loop in LIVE, turn Rec to HOLD, then sweep Pos slowly with Glide around 60 and stop on a snare. Grain 10..30 ms on a held synth chord gives a buzzy drone; Spray 30 makes it a cloud.

## Test tools (not in the pack)

### Metro: a click on Segue's beat grid
Built only to check, by ear, whether Segue's kick tracker locks to the sequencer. The input passes through and a metronome click is mixed in on every beat the tracker counts. The tracker is Segue's, with fixes found with Metro that Segue will get too: AUTO threshold hunts until it locks (a boomy kick on every beat used to count only once), a missed stretch no longer stops the tempo from ever locking, Thrsh uses its whole range, and with a fixed tempo pushed kicks can no longer drag the grid. Nothing clicks until the tempo is locked. The clicks sit about 13 ms ahead of the kick by design: that is where Segue's beats (and its cuts) are, in the quiet just before the kick. A steady, tiny flam is right; clicks that drift or wander are the problem to report.

| Knob | Range | What it does |
|---|---|---|
| Mix | 0..100 | DJ-style crossfade: input full up to 50, clicks full from 50, both full at 50. 100 = clicks only. |
| Click | BEAT, BAR, KICK | BEAT: every beat (800 Hz), bar starts higher (1.6 kHz), phrase starts highest (2.4 kHz). BAR: only bar and phrase starts. KICK: no grid, a 1 kHz click on every sound the detector takes for a kick, about 20 ms late, locked or not (missing clicks = kicks it misses, extra clicks = bass or toms it takes for kicks). |
| Bars | 1..8 | Phrase length for the phrase click, counted from bar 1 as Segue counts it. |
| LoBPM, HiBPM | 40..240 | Tempo range the tracker may lock to. Default both 160: a fixed tempo is the surest setting, and needed for syncopated kicks (tribe, tekno), because then only kicks within 6 % of a beat move the grid. |
| Listn | as Segue | Where the kick detector listens, as on Segue. |
| Thrsh | AUTO, 1..100 | AUTO starts strict and loosens every 4 s until it locks. 1 = anything counts as a kick, 50 = 2x jump, 100 = only hard kicks. |

Any footswitch press resets bar 1 to the next kick. That bar 1's click comes about 20 ms late (the kick has to be heard first); the following clicks are on time.
