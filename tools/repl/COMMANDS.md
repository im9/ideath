# iDEATH REPL — Command Reference

## Quick Start

```bash
make repl
```

```
ideath> osc saw 440
ideath> filter lp 1000 0.7
ideath> note C4
ideath> quit
```

## Editor Integration (TCP)

The REPL listens on `127.0.0.1:7777` for TCP connections, enabling
Cmd+Enter style workflows from any editor (like TidalCycles / SuperCollider).

```bash
# Send a single command
echo "osc saw 440" | nc localhost 7777

# Send multiple commands at once
printf "osc saw 440\nfilter lp 800\nnote C4\n" | nc localhost 7777
```

Write commands in your editor, select them, and send with a keybinding.
See the project wiki for VSCode / Vim integration examples.

## Signal Chain

```
Source (osc | wt | noise)
  → AdsrEnvelope
  → Biquad filter
  → BitCrusher
  → Saturation
  → DelayLine
  → master volume
  → output
```

Each stage can be independently enabled/disabled. LFO and Portamento act as modulators.

## Commands

### Source

| Command | Description |
|---------|-------------|
| `osc <saw\|square> <freq>` | Oscillator source |
| `wt <square\|saw\|tri\|sine> <freq>` | Wavetable source |
| `noise` | White noise source |

### Effects

| Command | Description |
|---------|-------------|
| `filter <lp\|hp\|bp> <freq> <Q>` | Biquad filter |
| `crush <bits> <downsample_rate>` | BitCrusher |
| `sat <drive>` | Saturation (tanh drive) |
| `delay <time_sec> <feedback>` | Delay line |

Any effect can be disabled with `<command> off` (e.g., `filter off`).

### Modulation

| Command | Description |
|---------|-------------|
| `lfo <sine\|tri\|square\|saw\|sh> <rate> <pitch\|filter\|vol> <depth>` | LFO modulation |
| `porta <time_sec>` | Portamento glide time |
| `env <attack> <decay> <sustain> <release>` | ADSR envelope |

LFO depth units: cents for pitch/filter targets, percentage for volume.

### Sequencer

| Command | Description |
|---------|-------------|
| `seq <notes...> [bpm]` | Start step sequencer (default 120 BPM) |
| `seq bpm <bpm>` | Change tempo while running |
| `seq stop` | Stop sequencer |

Notes can be note names (`C4`, `C#4`, `Bb3`) or frequencies (`440`).
Use `-` or `.` for rests. BPM is the last argument if it's a number > 20.

### Playback

| Command | Description |
|---------|-------------|
| `note <C4\|C#4\|Bb3\|freq>` | Trigger note (sets frequency + noteOn) |
| `release` | Release current note (noteOff) |
| `vol <0.0-1.0>` | Master volume |
| `stop` | Silence and reset all state (also stops sequencer) |

### System

| Command | Description |
|---------|-------------|
| `help` | Show available commands |
| `quit` / `exit` | Exit the REPL |

## Examples

```bash
# Chiptune square wave with bitcrusher
ideath> wt square 440
ideath> crush 4 8000
ideath> vol 0.3

# Filtered saw with vibrato
ideath> osc saw 440
ideath> filter lp 2000 0.7
ideath> lfo sine 5 pitch 50

# Bass with envelope and portamento
ideath> osc square 110
ideath> env 0.01 0.2 0.6 0.5
ideath> porta 0.1
ideath> note C2
ideath> note E2
ideath> note G2
ideath> release

# Noisy delay texture
ideath> noise
ideath> filter lp 800 2.0
ideath> delay 0.4 0.6
ideath> vol 0.2

# Arpeggio with envelope
ideath> wt sine 440
ideath> env 0.01 0.1 0.0 0.05
ideath> seq C4 E4 G4 C5 180

# Chiptune sequence with rests
ideath> wt square 440
ideath> crush 4 8000
ideath> env 0.01 0.05 0.0 0.02
ideath> seq C4 C4 - E4 G4 - C5 - 200
ideath> seq stop
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
