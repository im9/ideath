---
description: Benchmark a DSP primitive — measure ns/sample processing cost
user_invocable: true
---

# Primitive Benchmark

Measure the per-sample processing cost of an ideath primitive.

## Usage

`/bench <PrimitiveName> [samples]`

- `PrimitiveName`: e.g. `SVFilter`, `Oscillator`, `Reverb`
- `samples`: number of samples to process (default: 1000000)

Example: `/bench SVFilter 5000000`

## Instructions

1. Parse the primitive name and optional sample count from the argument. If no name given, ask the user.

2. Read the primitive's header (`include/ideath/<Name>.h`) to understand its API — what `prepare()`, `set*()`, and `process()` signatures look like.

3. Write a standalone benchmark file to `tests/bench_tmp.cpp`:

```cpp
#include <ideath/<Name>.h>
#include <chrono>
#include <cmath>
#include <cstdio>

int main()
{
    constexpr int N = <sample_count>;
    constexpr float kSampleRate = 48000.0f;

    ideath::<Name> dsp;
    dsp.prepare(kSampleRate);
    // Set reasonable parameters for the primitive

    // Warm up
    for (int i = 0; i < 1000; ++i)
    {
        float x = std::sin(2.0f * 3.14159265f * 440.0f * float(i) / kSampleRate);
        dsp.process(x);  // adapt call signature as needed
    }
    dsp.reset();

    // Benchmark
    auto start = std::chrono::high_resolution_clock::now();

    float acc = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        float x = std::sin(2.0f * 3.14159265f * 440.0f * float(i) / kSampleRate);
        acc += dsp.process(x);  // adapt call signature as needed
    }

    auto end = std::chrono::high_resolution_clock::now();
    double ns = std::chrono::duration<double, std::nano>(end - start).count();

    std::printf("%s: %.1f ns/sample (%d samples, sum=%.2f)\n",
                "<Name>", ns / N, N, acc);

    // Context: at 48kHz, budget is ~20833 ns/sample for a single effect
    double budgetPercent = (ns / N) / 20833.0 * 100.0;
    std::printf("CPU budget: %.2f%% of 48kHz mono\n", budgetPercent);

    return 0;
}
```

4. Adapt the benchmark code to the primitive's actual API:
   - If `process()` takes no input (e.g. Oscillator): call `process()` without args or with waveform param
   - If it's stereo output (e.g. Reverb): use the appropriate output struct
   - Set meaningful parameters (not defaults) to reflect real-world usage

5. Compile and run:
```bash
cd /Users/tn/src/libs/ideath
c++ -std=c++17 -O2 -I include -o build/bench_tmp tests/bench_tmp.cpp src/<Name>.cpp [dependencies if needed] && ./build/bench_tmp
```
   If the primitive depends on other source files (e.g. Voice depends on Oscillator, Envelope, etc.), include them in the compile command. Check `#include` directives in the source to find dependencies.

6. Clean up: `rm tests/bench_tmp.cpp build/bench_tmp`

7. Report results to the user:
   - ns/sample
   - % of 48kHz mono budget
   - Whether it's safe for real-time use (< 5% is excellent, < 20% is fine, > 50% needs attention)
