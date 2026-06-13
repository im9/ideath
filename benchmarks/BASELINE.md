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
| KarplusStrong::process             |           2.636 µs |        5.15 |
| ModalResonator::process (8 part.)  |           5.748 µs |       11.23 |
| ModalResonator::process (16 part.) |          11.631 µs |       22.72 |
| HarmonicOscillator::process (8 part.)  |      12.156 µs |       23.74 |
| HarmonicOscillator::process (32 part.) |     115.658 µs |      225.89 |
| BowedString::process (steady bow)  |           8.654 µs |       16.90 |
| LowPassGate::process (Decay)       |           2.246 µs |        4.39 |
| LowPassGateVoice::process (Decay)  |           2.946 µs |        5.75 |
| GranularProcessor::process         |          24.174 µs |       47.21 |
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
- The two `HarmonicOscillator` rows were measured 2026-06-12 on the same
  Apple M2 Max host. The 8-partial config (LOW + MID bands) is the
  expected slothrop Loom common case; the 32-partial config is the
  worst-case ceiling. Inner loop is dominated by `std::sin` (one call per
  alive non-zero-amp partial). At 32 partials this is ~10× the
  ModalResonator-16 cost — the cost is real and well-attributed to `sin`,
  not algorithmic. A sine LUT / polynomial approx is the obvious future
  optimisation (per the SIMD candidates list in `CLAUDE.md`); not landed
  in v1 to keep the implementation auditable.
- `BowedString` measured 2026-06-12 on the same Apple M2 Max host.
  Steady-state bow (loop has reached its limit cycle) is the realistic
  workload — at note-on the loop builds up over a few hundred ms but the
  per-sample cost is the same. Inner loop is two `DelayLine::readDelay`
  taps + `std::exp` + `std::tanh` + LP filter; comparable to KarplusStrong
  with one extra `exp` and one extra delay-line write. ≈ 3× KS's
  5.15 ns/sample, dominated by the second `DelayLine`.
- `LowPassGate` / `LowPassGateVoice` measured 2026-06-12 on the same
  host. Decay-stage workload is the dominant runtime (attack is sub-ms);
  inner loop is one `std::exp` (cutoff) + Biquad LP coefficient recompute
  (`setLowpass`) + one Biquad sample step + one multiply. `LowPassGateVoice`
  adds the Oscillator hot path (saw↔square morph, ~1.3 ns over the bare
  oscillator bench) on top.
