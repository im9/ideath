# ideath performance baseline

Recorded ns/sample numbers for every `process()` hot path exercised by
`benchmarks/bench_primitives.cpp`. Use these as the reference when evaluating
"is this a regression?" per the **>20% ns/sample** rule in `CLAUDE.md`.

## How to reproduce

```bash
make bench
```

`make bench` configures with `-DCMAKE_BUILD_TYPE=Release -DTN_DSP_BUILD_BENCH=ON`
and runs the Catch2 benchmark executable with 100 samples per case.
Bench parameters: **kSR = 48000 Hz**, **kBlock = 512 samples** per `BENCHMARK` iteration.

`ns/sample = (mean block time in µs × 1000) / 512`.

## Host machine

Absolute numbers are host-specific. The ratio between rows is what's portable,
and the ratio vs. a prior baseline on the **same host** is what the >20% rule
compares. When the host changes, re-measure the whole table in one pass rather
than comparing cross-host.

- CPU: Apple M2 Max (arm64)
- OS: macOS 26.3
- Compiler: whatever CMake picks up as the default system `CXX` (AppleClang at time of measurement)

## Baseline (2026-04-17)

| Benchmark                          | mean / 512 samples | ns / sample |
|------------------------------------|-------------------:|------------:|
| Oscillator::process (saw)          |           2.815 µs |        5.50 |
| Oscillator::process (square)       |           2.818 µs |        5.50 |
| Wavetable::process                 |           2.755 µs |        5.38 |
| UnisonOscillator::process          |           9.179 µs |       17.93 |
| Noise::process                     |           1.610 µs |        3.14 |
| LFO::process                       |           3.005 µs |        5.87 |
| Portamento::process                |           2.136 µs |        4.17 |
| AdsrEnvelope::process              |           2.305 µs |        4.50 |
| DecayEnvelope::process             |           0.629 µs |        1.23 |
| FunctionGenerator::process (linear)|           2.424 µs |        4.73 |
| FunctionGenerator::process (curved)|           2.581 µs |        5.04 |
| Biquad::process                    |           4.382 µs |        8.56 |
| SVFilter::process                  |           4.328 µs |        8.45 |
| CombFilter::process                |           2.393 µs |        4.67 |
| Saturation::tanhDrive              |           2.986 µs |        5.83 |
| Saturation::softClip               |           1.783 µs |        3.48 |
| Wavefolder::process                |           3.445 µs |        6.73 |
| BitCrusher::process                |           2.282 µs |        4.46 |
| DelayLine::process                 |           2.091 µs |        4.08 |
| TapeDelay::process                 |          11.000 µs |       21.48 |
| FeedbackBuffer::process            |           5.273 µs |       10.30 |
| Compressor::process                |           7.647 µs |       14.94 |
| PeakLimiter::process               |           3.376 µs |        6.59 |
| Reverb::process                    |          11.359 µs |       22.19 |
| HallReverb::process                |          56.732 µs |      110.80 |
| ShimmerReverb::process             |          48.479 µs |       94.69 |
| FMSynth::process                   |          19.455 µs |       38.00 |
| Voice::process                     |           4.694 µs |        9.17 |
| Voice::process (LP filter, Q=4)    |           6.315 µs |       12.33 |
| Osc → SVFilter → ADSR → Sat → Rvb  |          21.277 µs |       41.56 |

## Notes

- The three "big" primitives — **HallReverb**, **ShimmerReverb**, **FMSynth** —
  dominate the budget. Any SIMD or structural work should target these first
  (see "Performance / SIMD (future)" in `CLAUDE.md`).
- **Voice (LP filter, Q=4)** is the filter-on variant introduced alongside the
  Voice SVFilter migration. It costs ≈ +3.2 ns/sample over the filter-off path,
  which is the marginal cost of SVFilter's trapezoidal integrator in the Voice
  signal chain. Future filter changes to Voice should be judged against this
  row, not the filter-off row.
- Apple M2 Max numbers are **not** a stand-in for the A15 ceiling declared in
  `CLAUDE.md`. Once A15 on-device numbers are recorded, add a separate column
  here and carry the per-primitive ceiling forward to plugin integration.
- The two `FunctionGenerator` rows were measured 2026-05-25 on the same Apple
  M2 Max host alongside the rest of the table. The curved variant exercises
  `std::pow` per sample (curve = 0.7); the linear variant short-circuits the
  shaper and is one branch + one float add in the hot path.
