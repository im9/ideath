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
make clean     # remove build directory
```

## Architecture

```
include/ideath/ — public headers (the API)
src/            — implementation files
tests/          — Catch2 v3 unit tests
```

### Primitives (low-level, stateless or minimal state)
- **Biquad** — Direct Form II Transposed (LP/HP/BP, RBJ cookbook)
- **Oscillator** — Phase accumulator, saw/square/morph
- **DecayEnvelope** — Trigger → exponential decay (drums, percussive)
- **AdsrEnvelope** — Attack/Decay/Sustain/Release (sustained sounds)
- **Noise** — xorshift32 white noise generator
- **Saturation** — tanh drive + polynomial soft clip
- **Wavetable** — 4-bit wavetable oscillator (Game Boy style, nearest/linear interpolation)
- **BitCrusher** — Bit depth reduction + sample rate reduction (lo-fi digital)

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

## Integration with Plugin Projects

In the plugin's `CMakeLists.txt`:

```cmake
add_subdirectory(path/to/ideath)
target_link_libraries(MyPlugin PRIVATE ideath)
```

Then `#include <ideath/Biquad.h>` etc. in plugin code.

## Relationship to Other Projects

- **inboil** (`/Users/tn/src/front/inboil`) — browser DAW. DSP algorithms originate here, ported to C++
- **tr606** (`/Users/tn/src/vst/tr606`) — JUCE VST prototype, will link ideath as subdirectory
- **Future iOS/VST apps** — consume ideath for DSP, own UI is separate

## Docs, Code, and Commits

- All in English
- Commit messages: imperative mood, concise
