# iDEATH REPL — Command Reference

## Quick Start

```bash
make repl
```

```
ideath[1]> preset acid
ideath[1]> seq C3 C3 - E3 G3! - C4! - 140
ideath[1]> track 2
ideath[2]> preset hihat
ideath[2]> seq C4 C4 C4 C4 200
ideath[2]> track
```

## Editor Integration (TCP)

The REPL listens on `127.0.0.1:7777` for TCP connections, enabling
Cmd+Enter style workflows from any editor (like TidalCycles / SuperCollider).

```bash
# Send a single command
echo "osc saw 440" | nc localhost 7777

# Send multiple commands at once
printf "preset acid\nnote C3\n" | nc localhost 7777
```

Write commands in your editor, select them, and send with a keybinding.
See the project wiki for VSCode / Vim integration examples.

## Signal Chain

```
Source (osc | wt | noise | fm | unison | pluck | modal)
  → Filter
  → AdsrEnvelope
  → Compressor
  → BitCrusher
  → Saturation
  → Wavefolder
  → DelayLine
  → TapeDelay
  → CombFilter
  → Looper (FeedbackBuffer)
  → GranularProcessor
  → Reverb
  → master volume
  → output
```

Each stage can be independently enabled/disabled. LFO and Portamento act as modulators.
8 independent tracks, each with its own signal chain and sequencer.

## Commands

### Source

| Command | Description |
|---------|-------------|
| `osc <saw\|square> <freq>` | Oscillator source |
| `wt <square\|saw\|tri\|sine> <freq>` | Wavetable source |
| `noise` | White noise source |
| `fm <algo> [r1:l1] [r2:l2] [r3:l3] [r4:l4]` | FM synth source (algo 0-7) |
| `fmfb <op 0-3> <amount>` | Set FM operator feedback |
| `unison <saw\|square> <freq> [voices] [detune_cents]` | Unison oscillator (default 5 voices, 15 cents) |
| `pluck [freq] [decay_sec] [damping] [exciter]` | Karplus-Strong plucked string (defaults: 220 Hz, 1 s, 0.3, 1.0). Re-plucks on every `note` / sequencer step. |
| `modal <fund> [partials] [decay] [inharm]` | Bell / chime engine. `partials` 1-16, `decay` seconds (uniform across partials), `inharm` 0-1 stretches upper partials. Struck on every note-on / sequencer step. e.g. `modal 220 12 1.5 0.4` then `note C3`. |

FM operator format is `ratio:level` (e.g., `fm 0 1:1 2:0.5 3:0.3 4:0.2`).
Algorithms 0-7 define how the 4 operators modulate each other (YM2612-style).

### Effects

| Command | Description |
|---------|-------------|
| `filter <lp\|hp\|bp> <freq> <Q>` | Biquad filter |
| `crush <bits> <downsample_rate>` | BitCrusher |
| `sat <drive>` | Saturation (tanh drive) |
| `fold <drive> [mix]` | Wavefolder (sine-based, `fold off` to disable) |
| `delay <time_sec> <feedback>` | Delay line |
| `tape <time_sec> <feedback> [wowDepth] [wowRate] [flutterDepth] [flutterRate] [lp] [hp] [drive]` | Tape delay |
| `comb <time_sec> <feedback> [damp] [mix]` | Comb filter / resonator |
| `granular <rate_hz> <size_sec> [pitchSpread_st] [scatter] [mix]` | Granular cloud (2 s ring buffer) |
| `granular freeze [on\|off]` | Freeze granular ring buffer (no on/off arg toggles) |
| `loop <rec\|stop\|play\|dub\|off>` | Looper (record/overdub/play, up to 30s) |
| `loop feedback <0-1>` | Looper overdub feedback |
| `loop mix <0-1>` | Looper dry/wet mix |
| `comp <thresh_dB> <ratio> [attack] [release] [makeup_dB]` | Compressor |
| `comp off` | Disable compressor |
| `reverb <room\|hall\|shimmer> [size] [damp] [mix]` | Reverb effect |
| `reverb hall <size> <damp> <mix> <predelay> <moddepth>` | Hall with extra params |
| `reverb shimmer <size> <damp> <mix> <shimmer_amount>` | Shimmer with extra params |
| `reverb freeze` | Toggle reverb freeze |
| `reverb off` | Disable reverb |

Any effect can be disabled with `<command> off` (e.g., `filter off`).

### Modulation

| Command | Description |
|---------|-------------|
| `lfo <sine\|tri\|square\|saw\|sh> <rate> <pitch\|filter\|vol> <depth>` | LFO modulation |
| `fg <rise> <fall> <curve> <oneshot\|cycle> <pitch\|filter\|vol> <depth>` | West Coast function generator (0-Coast Contour, MATHS) |
| `fg off` | Disable function generator |
| `penv <semitones> <decay_sec>` | Pitch envelope sweep (e.g., `penv 36 0.04` for kicks) |
| `penv off` | Disable pitch envelope |
| `porta <time_sec>` | Portamento glide time |
| `env <attack> <decay> <sustain> <release>` | ADSR envelope |

LFO depth units: cents for pitch/filter targets, percentage for volume.
Function generator: `rise`/`fall` in seconds, `curve` in [-1, +1] (negative = log-ish
pluck, positive = exponential swell), `oneshot` re-fires on every note-on, `cycle`
free-runs as a shaped LFO. Depth: cents for pitch/filter, percent for volume.
Output is unipolar [0, 1] so pitch/filter targets always sweep upward from base
by `depth` cents at peak.
Pitch envelope sweeps frequency up by N semitones at note-on, then decays back to base pitch.
Essential for kick drums (e.g., `penv 36 0.04` = 3 octaves, 40ms decay).

### Presets

| Command | Description |
|---------|-------------|
| `preset <name>` | Load voice preset |
| `preset list` | Show available presets |

Available presets: `acid`, `chiptune`, `pad`, `kick`, `perc`, `bass`, `lead`, `hihat`, `ambient`, `lofi`.

`kick` uses pitch envelope (sine + 3-octave sweep + saturation).

Presets set source, envelope, filter, and effects. Current volume is preserved.

### Sequencer

| Command | Description |
|---------|-------------|
| `seq <notes...> [bpm]` | Start step sequencer |
| `seq bpm <bpm>` | Change tempo while running |
| `seq gate <percent>` | Gate length (1-100, default 80) |
| `seq reverse` | Reverse pattern |
| `seq shuffle` | Randomize step order |
| `seq rotate [n]` | Rotate pattern by n steps (default 1) |
| `seq stop` | Stop sequencer |

Notes can be note names (`C4`, `C#4`, `Bb3`) or frequencies (`440`).
Use `-` or `.` for rests. Append `!` for accent (e.g., `C4!`).
BPM is the last argument if it's a number > 20.

### Tracks

| Command | Description |
|---------|-------------|
| `track <n>` | Switch active track (1-8) |
| `track <n> mute` | Toggle track mute |
| `track <n> solo` | Toggle track solo |
| `track <n> vol <0.0-1.0>` | Set track volume |
| `track` | Show track status |

Each track has its own signal chain, presets, and sequencer.
All commands apply to the active track. Solo overrides mute.

### Limiter

| Command | Description |
|---------|-------------|
| `limiter` | Show limiter status and gain reduction |
| `limiter <dB>` | Set limiter threshold (e.g., `limiter -6`) |
| `limiter on` | Enable limiter |
| `limiter off` | Disable limiter (hard clamp fallback) |

Brickwall peak limiter on the master output (default: ON, 0 dB threshold, 5ms lookahead).

### Playback

| Command | Description |
|---------|-------------|
| `note <C4\|C#4\|Bb3\|freq>` | Trigger note (sets frequency + noteOn) |
| `release` | Release current note (noteOff) |
| `vol <0.0-1.0>` | Master volume (per-track) |
| `stop` | Silence and reset all state (also stops sequencer) |

### Scope

| Command | Description |
|---------|-------------|
| `scope` | Show oscilloscope snapshot (one-shot, Braille rendering) |
| `scope on` | Auto-refresh scope every 500ms (ANSI overwrite) |
| `scope off` | Stop auto-refresh scope |

Renders the master output waveform using Unicode Braille characters (2x4 dots per char).
`scope on` continuously updates in-place using ANSI cursor movement — useful for
monitoring sequencer output or tweaking parameters in real time.

### Spectrum

| Command | Description |
|---------|-------------|
| `spectrum` | Show FFT spectrum snapshot (one-shot, Braille rendering, log freq scale) |
| `spectrum on` | Auto-refresh spectrum every 500ms |
| `spectrum off` | Stop auto-refresh spectrum |

Renders the master output frequency spectrum using Unicode Braille characters.
Log frequency scale (20 Hz – Nyquist), dB magnitude (auto-scaled, 90 dB range).
Reference lines at 100 Hz, 1 kHz, 10 kHz. Can run simultaneously with `scope on`.

### System

| Command | Description |
|---------|-------------|
| `help` | Show available commands |
| `quit` / `exit` | Exit the REPL |

## Examples

```bash
# Acid bass line
ideath[1]> preset acid
ideath[1]> seq C3 C3 - Eb3 C3 F3! - C3 140

# Drum pattern on track 2
ideath[1]> track 2
ideath[2]> preset kick
ideath[2]> seq C2 - - - C2 - - - 140

# Hi-hat on track 3
ideath[2]> track 3
ideath[3]> preset hihat
ideath[3]> seq C4 C4 C4 C4 C4 C4 C4 C4 280

# Check all tracks
ideath[3]> track

# Mute the drums
ideath[3]> track 2 mute

# Reverse the bass pattern
ideath[3]> track 1
ideath[1]> seq reverse

# Chiptune square wave with bitcrusher
ideath[1]> wt square 440
ideath[1]> crush 4 8000
ideath[1]> vol 0.3

# Filtered saw with vibrato
ideath[1]> osc saw 440
ideath[1]> filter lp 2000 0.7
ideath[1]> lfo sine 5 pitch 50

# Bass with envelope and portamento
ideath[1]> osc square 110
ideath[1]> env 0.01 0.2 0.6 0.5
ideath[1]> porta 0.1
ideath[1]> note C2
ideath[1]> note E2
ideath[1]> release

# Arpeggio with accents
ideath[1]> wt sine 440
ideath[1]> env 0.01 0.1 0.0 0.05
ideath[1]> seq C4 E4! G4 C5! 180

# Short gate staccato
ideath[1]> preset chiptune
ideath[1]> seq C4 E4 G4 C5 160
ideath[1]> seq gate 30

# Visualize current waveform
ideath[1]> osc saw 440
ideath[1]> scope

# Plucked string melody (Karplus-Strong)
ideath[1]> pluck 220 1.5 0.25 1.0
ideath[1]> seq C3 E3 G3 B3 C4 B3 G3 E3 110

# Granular cloud over a held pad
ideath[1]> preset pad
ideath[1]> note C3
ideath[1]> granular 40 0.08 5 0.5 0.7    # 40 grains/sec, 80 ms, ±5 st, mid scatter, 70% wet
ideath[1]> granular freeze on            # capture current buffer, keep scrubbing
```

## Note Names

Standard notation: letter + optional `#`/`b` + octave (0-9).

| Note | Frequency |
|------|-----------|
| C4 | 261.63 Hz |
| A4 | 440.00 Hz |
| C#4 | 277.18 Hz |
| Bb3 | 233.08 Hz |

Alternatively, pass a frequency directly: `note 440`.
