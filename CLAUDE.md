# iDEATH

DSP foundation library. Pure C++17, zero external runtime dependencies.
Designed to be shared across JUCE plugins (VST/AU/iOS) and potentially Emscripten targets.

Named after a place in Richard Brautigan's *In Watermelon Sugar*.

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
Interactive live-coding environment for auditioning primitives. 8 independent tracks,
each with its own signal chain, sequencer, and presets. Master output protected by
PeakLimiter. Commands are documented in `tools/repl/COMMANDS.md`.
Built with miniaudio (vendored in `third_party/miniaudio/`).
CMake option: `TN_DSP_BUILD_REPL=ON`.

**REPL as reference implementation:** The REPL is not just a demo — it is the reference
audio engine for the ideath ecosystem. Downstream plugin and iOS targets should
be able to use REPL output as the "correct" sound for a given set of parameters.
Therefore REPL audio quality must be production-grade: no clicks, no artifacts, no
audible glitches in any signal chain configuration. When a bug is heard in the REPL,
it must be fixed at the root — not worked around.

**Known issues (WIP):** Click noise on sequencer note retrigger with resonant
filter + saturation was fixed by reordering the signal chain to standard
subtractive routing (Osc → Filter → Envelope) and adding a ~1ms retrigger
fade to AdsrEnvelope. Monitor for regressions in extreme parameter combos.

**REPL exposure guideline:** Every primitive that produces or transforms audio should
be accessible as a REPL command. Primitives that only bundle other primitives (Voice,
Polyphony) are excluded because the REPL itself serves that role.

### Primitives (low-level, stateless or minimal state)
- **Biquad** — Direct Form II Transposed (LP/HP/BP, RBJ cookbook)
- **Oscillator** — Phase accumulator, saw/square/morph
- **DecayEnvelope** — Trigger → exponential decay (drums, percussive)
- **AdsrEnvelope** — Attack/Decay/Sustain/Release (sustained sounds, optional curve bend on attack/release)
- **AREnvelope** — Attack/Sustain/Release (slow per-layer fades, gates)
- **Noise** — xorshift32 white noise generator
- **BandlimitedNoise** — xorshift32 noise + one-pole LP (Bandwidth: white → pink-ish → brown → random walk)
- **Saturation** — tanh drive + polynomial soft clip
- **Wavetable** — Wavetable oscillator (4-bit Game Boy style or arbitrary normalized data, nearest/linear interpolation)
- **BitCrusher** — Bit depth reduction + sample rate reduction (lo-fi digital)
- **DelayLine** — Circular buffer delay with linear interpolation, feedback, dry/wet mix
- **TapeDelay** — Tape-style delay with wow/flutter modulation, feedback LP/HP coloring, saturation
- **LFO** — Low-frequency oscillator (sine/tri/square/saw/S&H, uni/bipolar, one-shot, Shape/Curve/Quantize morph)
- **Portamento** — Exponential pitch/value glide
- **Voice** — Single synth voice (source + ADSR + filter + LFO + effects chain)
- **Polyphony** — Multi-voice manager (pool allocation, voice stealing, mixing)
- **FMSynth** — 4-operator FM synthesizer (8 algorithms, per-op ADSR/feedback, YM2612-inspired)
- **Reverb** — Freeverb (8 comb + 4 allpass, size/damp/freeze, stereo out)
- **HallReverb** — Pre-delay + LFO-modulated Freeverb (size/damp/preDelay/modDepth/freeze, stereo out)
- **ShimmerReverb** — Cross-coupled allpass network + octave pitch shift feedback (size/damp/shimmer/freeze, stereo out)
- **PeakLimiter** — Lookahead brickwall limiter (threshold/release/lookahead)
- **Compressor** — Peak envelope compressor (threshold/ratio/attack/release/makeup/knee)
- **Wavefolder** — sin(input * drive) wavefolder (West Coast timbre shaping, drive/mix)
- **FeedbackBuffer** — Long circular buffer looper (record/overdub/playback, feedback/mix)
- **UnisonOscillator** — Stacked detuned oscillators (voice count, detune spread in cents)
- **SVFilter** — Trapezoidal integrated SVF (LP/HP/BP/Notch, modulation-safe, Cytomic TPT)
- **CombFilter** — Feedback comb filter with damping (Karplus-Strong / metallic textures)
- **FormantFilter** — 3 parallel bandpass SVF, vowel morph A-E-I-O-U (vocal character shaping)
- **HarmonicWavetable** — 128-table morphing wavetable (additive harmonic series, band-limited, continuous morph)
- **FunctionGenerator** — West Coast rise/fall envelope (Make Noise 0-Coast Contour, MATHS rise/fall), shared curve, one-shot or self-cycling LFO, end-of-cycle pulse for inter-module routing
- **KarplusStrong** — Plucked-string synthesis (noise burst → LP-filtered delay-line feedback), freq / decay / damping / exciter; loop gain compensates for filter loss so the -60 dB tail length matches `setDecay` even under heavy damping
- **ModalResonator** — Bell engine: N parallel BPs (modes) with per-partial Q derived from decay (`Q = π × fc × decay / ln(1000)`), struck by a short noise burst; piano-string inharmonicity stretch (`f_i = fundamental × ratio_i × sqrt(1 + B × ratio_i²)`); Nyquist-muted partials
- **GranularProcessor** — Granular cloud (ring buffer + Hann-windowed grain pool), grain rate / size / pitch spread / position scatter / freeze; pool-exhaustion graceful; deterministic given a fixed RNG seed
- **HarmonicOscillator** — Plaits-style additive engine: sum of up to 32 harmonically-related sines, Plaits-style LOW/MID/HIGH band amplitudes with within-band taper (`shape`), per-partial amplitude override; partials above Nyquist silently muted; deterministic phase init

### Design Principles
- **JUCE-free** — no JUCE headers in the library; JUCE stays in the plugin layer
- **Real-time safe** — no allocation after construction, no exceptions, no locks
- **Mono-first** — primitives process mono (`float` in/out); stereo routing, panning, and M/S are the plugin layer's responsibility. Exception: algorithms that are inherently stereo (e.g. Reverb, PingPongDelay) may output L/R
- **No mod matrix** — modulation routing is the plugin layer's responsibility. Primitives just expose per-sample-safe setters; the plugin calls `set<Param>()` each sample with base + mod values
- **Testable** — every primitive has Catch2 tests with absolute-level assertions
- **Namespace** — everything lives under `ideath::`
- **Output levels** — most primitives output ±1.0 for ±1.0 input, but resonant filters and reverbs can exceed this significantly. The plugin layer MUST place a PeakLimiter after the signal chain. Per-primitive output ceilings for ±1.0 input:

  | Primitive | Max output | Cause |
  |-----------|-----------|-------|
  | Reverb / HallReverb | ±1.5 | kWetScale=3, multi-comb sum |
  | ShimmerReverb | ±6.0 | pitch-shift feedback regeneration + kWetScale=3 |
  | FormantFilter | ±Q (up to ±10) | 3 parallel BPs, Q=1/k at max resonance |
  | SVFilter | ±Q (up to ±5) | resonant peak at cutoff, Q=1/(2−2×resonance) |
  | Biquad | ±Q | resonant peak, Q passed directly |
  | UnisonOscillator | ±√N (up to ±4) | N voices in-phase, gain comp ÷√N |
  | ModalResonator | ±N (up to ±16) | N parallel BPs with per-partial Q derived from decay; BP output multiplied by Q to compensate 0 dB-peak normalisation → per-partial peak ≈ 1, N partials sum to ≈ ±N in worst-case phase alignment |
  | HarmonicOscillator | ±N (up to ±32) | N partials at amp 1.0 sum to ±N in worst-case phase alignment; typical observed peak ≈ √(N/2)·√(2 ln M) ≈ ±18 over a 1 s window of random-phase init |
  | All others | ±1.0 | oscillators, envelopes, noise, saturation, delay, etc. |

## Conventions

- C++17 (matches JUCE plugin projects)
- TDD: write failing tests first, then implement
- All DSP parameters are raw physical units (Hz, seconds, dB) — normalization happens in the plugin layer
- Header in `include/ideath/`, implementation in `src/`, test in `tests/test_<Name>.cpp`
- No `using namespace` in headers
- **Denormal protection** — every primitive with feedback state must guard against denormalized floats. Two patterns:
  - **DC offset** (`+ 1e-25f`): for linear feedback state (filters, delay buffers). Applied to: SVFilter, Biquad, Reverb combs/allpasses, HallReverb, ShimmerReverb, DelayLine, FeedbackBuffer.
  - **Flush-to-zero threshold**: for exponential decays (envelopes, compressor). Applied to: DecayEnvelope, AdsrEnvelope, PeakLimiter, Compressor, Portamento.
  - New primitives with feedback MUST use one of these patterns. See `eede258` for the original reference.
- **Parameter clamping** — setters must clamp to valid ranges (`std::clamp`, `std::max`) before storing or computing coefficients. Frequencies to `[minHz, sampleRate * 0.45]`, Q/resonance to `[floor, ceiling]`, time values to `[small positive, max]`. Never trust caller input in `set<Param>()`.
- **Phase wrapping** — phase accumulators must wrap via `phase -= std::floor(phase)` every sample to stay in `[0, 1)`. Prevents float precision loss over long playback. See `bd500ec` for a case where missing wrap caused drift.
- **Crossfades between decorrelated signals** — use equal-power (`cos²+sin²=1`), not linear. A linear crossfade between two uncorrelated signals produces a ~3 dB RMS dip at the midpoint; equal-power keeps the summed RMS constant. Applies to ShimmerReverb freeze blend (`src/ShimmerReverb.cpp`) and FeedbackBuffer loop-seam (`src/FeedbackBuffer.cpp`); a lookup table is OK for real-time paths that can't afford `cos` / `sin` per sample. See commits `c924a7f`, `87d001c`.

## Adding or Changing a Primitive

1. Create `include/ideath/Foo.h` with the interface
2. Create `tests/test_Foo.cpp` with tests (must fail initially)
3. Create `src/Foo.cpp` with the implementation
4. Add source files to `CMakeLists.txt` (`target_sources` for lib, `add_executable` for tests)
5. `make test` — all green
6. Update Primitives list in `CLAUDE.md` and `README.md`
7. **Run `/audit` (the audit skill)** — required for any new primitive or non-trivial change to an existing one.  History has shown narrow per-file review misses entire bug classes (canonical-reference omissions, reversed signs that match a correct comment, switch/case copy-paste, fixes that were applied to the REPL but not the library).  The audit skill at `.claude/skills/audit.md` is structured around the bug families that have actually shown up here.
8. **Run `make bench`** — required for any new primitive or change that touches a hot loop / `process()` body.  The bench suite at `benchmarks/bench_primitives.cpp` exists to catch performance regressions before they reach the downstream plugin / iOS targets where headroom is tight.

Steps 7 and 8 are not optional polish — they are part of the definition of "done" for primitive work.  Skipping them is how we got the bugs that prompted this checklist.  When in doubt about whether a change qualifies as "non-trivial", run them anyway; they're cheap compared to debugging a regression in the plugin layer.

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
- **Threshold justification** — every numeric threshold (tolerance, bounds, RMS, timing) must have a derivation in the adjacent comment: physics formula, mathematical identity, or spec value. "Allow ±5%" requires stating what the 5% is relative to and why 5% and not 1% or 20%. Setting a threshold by running the implementation and using the observed value is prohibited — that is fitting the test to the code, not specifying behavior.
- **Parameter boundary behavior** — test that clamped extremes produce musically/physically correct results, not just "no crash". Examples: cutoff at 0 Hz → near-silence, cutoff at Nyquist → near-passthrough, feedback at 0.0 → no recirculation, Q at minimum → flat response.
- **Long-run stability** — primitives with feedback or phase accumulators must be tested for at least 10 seconds of continuous processing to catch precision drift and denormal accumulation that 1-second tests miss.
- **Extreme parameter combinations** — test pairs or triples of extreme parameters together (high Q × high cutoff × hot input, max feedback × max delay × max drive, etc.). Individual-parameter edge cases are necessary but not sufficient for a shared DSP library.

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

## Next Steps

### Primitives — Effects (outstanding ports)
Shipped primitives are listed in the **Primitives** section at the top of this
file. Outstanding ports:
- [ ] StutterBuffer — slice repeat glitch, crossfade boundaries
- [ ] Distortion — overdrive (tube asymmetric) + fuzz (hard clip) flavors

### Robustness / Refactoring
- [ ] ShimmerReverb — Freverb is processed every sample even when freeze is inactive (pure CPU waste for sessions that never touch freeze). Naive skip breaks the "capture current tail on freeze press" semantic (Freverb would hold stale/empty state); needs either (a) a pre-warm window right before freeze engages, or (b) an explicit API to opt out of freeze support. Scope ~30–60 min + test
- [ ] UnisonOscillator — improve gain compensation (account for waveform harmonic richness)
- [ ] Library-wide NaN hardening of setters — every primitive's `set<Param>` uses `std::clamp` / `std::max` for finite-range enforcement, but all of these propagate NaN per IEEE 754 (`std::max(NaN, x)` → NaN, `std::clamp(NaN, …)` → NaN). A single NaN from an upstream modulation source corrupts filter coefficients / delay indices / envelope state permanently. Decide policy: (a) add an `isfinite`-guarded front door at each setter, (b) rely on the plugin layer to sanitise, or (c) treat NaN as user error and document it. Current behaviour matches Biquad's pre-migration behaviour (not a regression), but worth a deliberate decision

### Performance baseline
Minimum target: **iPhone 14 (A15), 128 samples @ 44.1kHz** (≈2.9ms buffer).
ideath must deliver performance that fits this budget. The ns/sample baseline
for each primitive is measured by `make bench` and recorded in
`benchmarks/BASELINE.md`. The actual ns/sample ceiling per primitive will be
established from A15 real-device profiling (TODO) and becomes a fixed library
constraint once set.

Rules:
- Any new primitive or change to a `process()` hot path must update the baseline.
- A regression of **>20% ns/sample** vs baseline is a blocking issue — investigate before merging. (20% is a provisional threshold before A15 profiling; revisit once the per-primitive ceiling is established.)
- The baseline is measured on the development machine; absolute values are not portable, but relative changes are meaningful.

### Performance / SIMD (future)
Identified candidates, ordered by expected ROI:
- [ ] Reverb/HallReverb comb banks — 8 independent filters, process 4 in parallel (SSE) → 3-6x
- [ ] UnisonOscillator voice loop — up to 16 oscillators, independent phases → 3-4x
- [ ] Polyphony voice mix — process 4 voices in parallel with active mask → 2-4x
- [ ] ShimmerReverb allpass chains — L/R independent, process as stereo pair → 2-4x
- [ ] Wavetable sine generation — one-time init loop, trivially vectorizable → 4-8x

Prerequisites: benchmark suite (Catch2 BENCHMARK) to measure before/after.
Approach: start with `-march=native` compiler flag + `#pragma omp simd` for
low-hanging fruit. SoA refactoring for Polyphony/Reverb only if benchmarks justify.

### Test threshold audit
Every numeric threshold (tolerances, bounds, RMS, timing) in the test suite
must have a physical or mathematical justification. A threshold without a
reason is a threshold that might have been fitted to a broken implementation.

Audit cadence: review one file at a time. A file is marked audited only after
every threshold in it has a stated derivation. Per-file audit status and a
compressed summary of each file's key derivations live in
[tests/AUDIT.md](tests/AUDIT.md) — consult that index when a test fails with
an unexpected delta and you need to know where a bound came from.

When a new test file is committed, add it to the **Unaudited** section of
`tests/AUDIT.md` and graduate it to the main table once its derivations are
documented.

### Other
- [ ] Plugin project — JUCE VST3/AU or iOS AUv3

## Docs, Code, and Commits

- All in English
- Commit messages: imperative mood, concise
