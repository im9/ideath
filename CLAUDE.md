# iDEATH

Personal DSP foundation library. Pure C++17, zero external runtime dependencies.
Designed to be shared across JUCE plugins (VST/AU/iOS) and potentially Emscripten targets.

Named after a place in Richard Brautigan's *In Watermelon Sugar* (same universe as inboil).

## Setup

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

## Build & Test

```bash
make build     # configure + build
make test      # build + run tests
make repl      # build + launch interactive REPL
make clean     # remove build directory
```

## Architecture

```
include/ideath/ — public headers (the API)
src/            — implementation files
tests/          — Catch2 v3 unit tests
tools/repl/     — interactive REPL tool (miniaudio, real-time audio)
```

### REPL Tool
Interactive live-coding environment for auditioning primitives. Builds a single-voice
signal chain: Source → Envelope → Filter → BitCrusher → Saturation → Delay → output.
Commands are documented in `tools/repl/COMMANDS.md`.
Built with miniaudio (vendored in `third_party/miniaudio/`).
CMake option: `TN_DSP_BUILD_REPL=ON`.

### Primitives (low-level, stateless or minimal state)
- **Biquad** — Direct Form II Transposed (LP/HP/BP, RBJ cookbook)
- **Oscillator** — Phase accumulator, saw/square/morph
- **DecayEnvelope** — Trigger → exponential decay (drums, percussive)
- **AdsrEnvelope** — Attack/Decay/Sustain/Release (sustained sounds)
- **Noise** — xorshift32 white noise generator
- **Saturation** — tanh drive + polynomial soft clip
- **Wavetable** — Wavetable oscillator (4-bit Game Boy style or arbitrary normalized data, nearest/linear interpolation)
- **BitCrusher** — Bit depth reduction + sample rate reduction (lo-fi digital)
- **DelayLine** — Circular buffer delay with linear interpolation, feedback, dry/wet mix
- **LFO** — Low-frequency oscillator (sine/tri/square/saw/S&H, uni/bipolar, one-shot)
- **Portamento** — Exponential pitch/value glide
- **Voice** — Single synth voice (source + ADSR + filter + LFO + effects chain)
- **Polyphony** — Multi-voice manager (pool allocation, voice stealing, mixing)

### Design Principles
- **JUCE-free** — no JUCE headers in the library; JUCE stays in the plugin layer
- **Real-time safe** — no allocation after construction, no exceptions, no locks
- **Testable** — every primitive has Catch2 tests with absolute-level assertions
- **Namespace** — everything lives under `ideath::`

## Conventions

- C++17 (matches JUCE plugin projects)
- TDD: write failing tests first, then implement
- All DSP parameters are raw physical units (Hz, seconds, dB) — normalization happens in the plugin layer
- Header in `include/ideath/`, implementation in `src/`, test in `tests/test_<Name>.cpp`
- No `using namespace` in headers

## Adding a New Primitive

1. Create `include/ideath/Foo.h` with the interface
2. Create `tests/test_Foo.cpp` with tests (must fail initially)
3. Create `src/Foo.cpp` with the implementation
4. Add source files to `CMakeLists.txt` (`target_sources` for lib, `add_executable` for tests)
5. `make test` — all green
6. Update Primitives list in `CLAUDE.md` and `README.md`

## Testing Conventions

- Every primitive must have corresponding tests in `tests/test_<Name>.cpp`
- Tests must verify at minimum:
  - **Output range** — bounds checks (e.g., `[-1, 1]`)
  - **Expected behavior** — correct frequency, waveform shape, timing, etc.
  - **Reset/state** — `reset()` returns to initial state
  - **Edge cases** — parameter clamping, zero/extreme values
- Use absolute assertions: `REQUIRE`, `REQUIRE_THAT(x, WithinAbs(y, tol))`
- Tag all tests with the primitive name: `[osc]`, `[wavetable]`, `[delay]`, etc.
- No allocation in test loops (mirrors real-time constraint)

## Primitive API Conventions

Every primitive follows the same interface pattern:

- `prepare(float sampleRate)` — initialize with sample rate, call `reset()`
- `reset()` — return to initial state (zero phase, clear buffers)
- `set<Param>(...)` — parameter setters, precompute coefficients
- `process(...)` — per-sample processing, returns `float`
- Parameters are raw physical units (Hz, seconds, dB)
- No allocation in `process()` — real-time safe
- Default-constructible with sensible defaults (44100 Hz sample rate)

## Integration with Plugin Projects

In the plugin's `CMakeLists.txt`:

```cmake
add_subdirectory(path/to/ideath)
target_link_libraries(MyPlugin PRIVATE ideath)
```

Then `#include <ideath/Biquad.h>` etc. in plugin code.

## Relationship to Other Projects

- **inboil** (https://github.com/im9/inboil) — browser DAW. DSP algorithms originate here, ported to C++
- **Future iOS/VST apps** — consume ideath for DSP, own UI is separate

## Next Steps

- [x] REPL: sequencer (`seq C4 E4 G4 120`)
- [x] REPL: optimize filter coefficient recalculation (only on parameter change)
- [x] Voice class — bundle primitives into a single voice (Wavetable + Env + Filter + LFO)
- [x] Polyphony management — multi-voice allocation (Junior supports up to 16)
- [ ] Plugin project — JUCE VST3/AU or iOS AUv3
- [ ] FM Synth primitive — hardware-inspired (YM2612/SID reference), needs design
- [ ] Reverb primitive — needs careful design for generality

## Docs, Code, and Commits

- All in English
- Commit messages: imperative mood, concise
