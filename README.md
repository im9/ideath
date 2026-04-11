# iDEATH

Personal DSP foundation library. Pure C++17, zero external runtime dependencies.

Designed to be shared across JUCE plugins (VST/AU/iOS) and potentially Emscripten targets.

Named after a place in Richard Brautigan's *In Watermelon Sugar*.

## Primitives

| Module | Description |
|--------|-------------|
| **Biquad** | Direct Form II Transposed (LP/HP/BP, RBJ cookbook) |
| **Oscillator** | Phase accumulator, saw/square/morph |
| **DecayEnvelope** | Trigger → exponential decay (drums, percussive) |
| **AdsrEnvelope** | Attack/Decay/Sustain/Release (sustained sounds, optional curve bend on attack/release) |
| **AREnvelope** | Attack/Sustain/Release (slow per-layer fades, gates) |
| **Noise** | xorshift32 white noise generator |
| **BandlimitedNoise** | xorshift32 noise + one-pole LP (Bandwidth: white → pink-ish → brown → random walk) |
| **Saturation** | tanh drive + polynomial soft clip |
| **Wavetable** | Wavetable oscillator (4-bit Game Boy style or arbitrary normalized data, nearest/linear interpolation) |
| **BitCrusher** | Bit depth reduction + sample rate reduction (lo-fi digital) |
| **DelayLine** | Circular buffer delay with linear interpolation, feedback, dry/wet mix |
| **TapeDelay** | Tape-style delay with wow/flutter modulation, feedback tone shaping, saturation |
| **LFO** | Low-frequency oscillator (sine/tri/square/saw/S&H, uni/bipolar, one-shot, Shape/Curve/Quantize morph) |
| **Portamento** | Exponential pitch/value glide |
| **Voice** | Single synth voice (source + ADSR + filter + LFO + effects chain) |
| **Polyphony** | Multi-voice manager (pool allocation, voice stealing, mixing) |
| **FMSynth** | 4-operator FM synthesizer (8 algorithms, per-op ADSR/feedback, YM2612-inspired) |
| **Reverb** | Freeverb stereo reverb (8 comb + 4 allpass, size/damp/freeze) |
| **HallReverb** | Hall reverb with pre-delay and LFO-modulated combs (lush, evolving tail) |
| **ShimmerReverb** | Shimmer reverb with octave pitch-shifted feedback (ethereal, metallic) |
| **PeakLimiter** | Lookahead brickwall peak limiter (threshold/release/lookahead) |
| **Compressor** | Peak envelope compressor (threshold/ratio/attack/release/makeup/knee) |
| **Wavefolder** | sin(input * drive) wavefolder for West Coast timbre shaping |
| **FeedbackBuffer** | Long circular buffer looper (record/overdub/playback) |
| **UnisonOscillator** | Stacked detuned oscillators with optional analog pitch drift (unison spread) |
| **SVFilter** | Trapezoidal integrated SVF (LP/HP/BP/Notch, modulation-safe) |
| **CombFilter** | Feedback comb filter with damping for resonator / Karplus-Strong tones |
| **FormantFilter** | 3 parallel bandpass SVF, vowel morph A-E-I-O-U |
| **HarmonicWavetable** | 128-table morphing wavetable, additive harmonic series, band-limited, continuous morph |

## Design Principles

- **JUCE-free** — no JUCE headers in the library; JUCE stays in the plugin layer
- **Real-time safe** — no allocation after construction, no exceptions, no locks
- **Mono-first** — primitives process mono (`float` in/out); stereo routing and panning are the plugin layer's responsibility. Inherently stereo algorithms (Reverb, PingPongDelay) may output L/R
- **No mod matrix** — modulation routing lives in the plugin layer; primitives expose per-sample-safe setters
- **Testable** — every primitive has Catch2 tests with absolute-level assertions
- **Raw units** — parameters are Hz, seconds, dB; normalization happens in the plugin layer

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

Or with the provided Makefile:

```bash
make build     # configure + build
make test      # build + run tests
make repl      # build + launch interactive REPL
make clean     # remove build directory
```

## Usage

In your plugin's `CMakeLists.txt`:

```cmake
add_subdirectory(path/to/ideath)
target_link_libraries(MyPlugin PRIVATE ideath::ideath)
```

```cpp
#include <ideath/Biquad.h>
#include <ideath/Oscillator.h>
// ...
```

Installed package usage:

```cmake
find_package(ideath CONFIG REQUIRED)
target_link_libraries(MyPlugin PRIVATE ideath::ideath)
```

## Interactive REPL

A live-coding environment for auditioning primitives in real time. Type commands to build a signal chain and hear the results immediately through your speakers.

```bash
make repl
```

```
ideath> wt square 440
ideath> filter lp 1000 0.7
ideath> crush 4 8000
ideath> delay 0.3 0.4
ideath> note C4
ideath> stop
```

See [tools/repl/COMMANDS.md](tools/repl/COMMANDS.md) for the full command reference.

## Project Structure

```
include/ideath/ — public headers (the API)
src/            — implementation files
tests/          — Catch2 v3 unit tests
tools/repl/     — interactive REPL tool (miniaudio)
```

## Assessment

See [ASSESSMENT.md](ASSESSMENT.md) for a candid evaluation of the project from technical, OSS, and product-foundation perspectives.

## Project Docs

- [CHANGELOG.md](CHANGELOG.md)
- [CONTRIBUTING.md](CONTRIBUTING.md)
- [VERSIONING.md](VERSIONING.md)
- [CLAUDE.md](CLAUDE.md)

## License

[MIT](LICENSE)
