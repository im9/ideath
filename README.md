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
| **AdsrEnvelope** | Attack/Decay/Sustain/Release (sustained sounds) |
| **Noise** | xorshift32 white noise generator |
| **Saturation** | tanh drive + polynomial soft clip |
| **Wavetable** | 4-bit wavetable oscillator (Game Boy style, nearest/linear interpolation) |
| **BitCrusher** | Bit depth reduction + sample rate reduction (lo-fi digital) |
| **DelayLine** | Circular buffer delay with linear interpolation, feedback, dry/wet mix |
| **LFO** | Low-frequency oscillator (sine/tri/square/saw/S&H, uni/bipolar, one-shot) |
| **Portamento** | Exponential pitch/value glide |

## Design Principles

- **JUCE-free** — no JUCE headers in the library; JUCE stays in the plugin layer
- **Real-time safe** — no allocation after construction, no exceptions, no locks
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
make clean     # remove build directory
```

## Usage

In your plugin's `CMakeLists.txt`:

```cmake
add_subdirectory(path/to/ideath)
target_link_libraries(MyPlugin PRIVATE ideath)
```

```cpp
#include <ideath/Biquad.h>
#include <ideath/Oscillator.h>
// ...
```

## Project Structure

```
include/ideath/ — public headers (the API)
src/            — implementation files
tests/          — Catch2 v3 unit tests
```

## License

[MIT](LICENSE)
