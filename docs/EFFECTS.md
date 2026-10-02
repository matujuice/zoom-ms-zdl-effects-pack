# The five effects

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
