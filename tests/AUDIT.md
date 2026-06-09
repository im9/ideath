# Test threshold audit log

Every numeric threshold (tolerance, bounds, RMS, timing) in the test suite must
have a physical or mathematical derivation. A threshold without a reason is a
threshold that might have been fitted to a broken implementation — the iDEATH
project considers fitting-to-impl the single most dangerous TDD anti-pattern
(see `CLAUDE.md` → "Threshold justification" + "TDD is non-negotiable" memory).

This file records the per-file audit status and a compressed summary of each
file's key derivations. The full derivations live as adjacent comments in the
test source; this file is the navigable index you consult when a test fails
with an unexpected delta and you need to know "where did that 5% come from?"

## Per-file audit summary

Review cadence: one file at a time. A file is marked audited only after every
threshold in it has a stated derivation (formula, physical identity, spec
value, or ULP argument). When a new primitive is added, its test file starts
life in this index as unaudited and is added to the table once the derivation
pass is done.

| Test file | Key derivations |
|---|---|
| `test_Oscillator` | zero-crossing range [836, 924] = 2 crossings/cycle × 440 Hz ± 5% |
| `test_Wavetable` | 4-bit normalization values; frequency correctness margins |
| `test_HarmonicWavetable` | aliasing bounds; morph continuity tolerance |
| `test_UnisonOscillator` | gain compensation bounds (±3.0); RMS thresholds |
| `test_Biquad` | dB attenuation tolerances; frequency response margins |
| `test_SVFilter` | resonance peak bounds; cutoff accuracy |
| `test_CombFilter` | Karplus-Strong decay thresholds |
| `test_FormantFilter` | vowel energy ratios; resonance bounds |
| `test_Envelope` | −60 dB coef convention; peak/sustain bit-exact; curve midpoints from `pow()`; retrigger timing/delta from retriggerCoef ≈ 0.8549 |
| `test_LFO` | analytical waveform bounds [−1, 1] + 1 ULP; DC tolerance 5e−4; saw/square/quantize counts from analytical cycle structure; +stability/extreme tests |
| `test_Portamento` | convergence tolerances from exp(−5N/samples); float-ULP plateau near ±1 documented; +monotonic-convergence/extreme tests |
| `test_Noise` | mean/RMS tolerances from uniform [−1, 1] σ = 1/√3; histogram bucket bound √(Np(1−p)); +histogram/stability/seed-diversity tests |
| `test_BandlimitedNoise` | hfRMS = α·σ·√(2 / (2 − α)) at each bandwidth; log-mapped fc formula; +stability and LP-settling tests |
| `test_Saturation` | tanh near-linear tolerance from Taylor remainder (x³/3); softClip range [−2/3, +2/3]; closed-form cubic matched to 1e−6; +monotonic/odd/asymptotic tests |
| `test_BitCrusher` | 32-bit passthrough tol from float ULP (1e−6); 1-bit exactly {−1, +1}; 4-bit 16 levels on {2k/15 − 1} lattice; downsample hold count via float32 sim (899 ± 3); +stability test |
| `test_Wavefolder` | sin Taylor bound (x³/6) for near-linear; output ±1 from convex combination; fold count at drive=8 from sin(8x) extrema (8 decreases); mix=0 bit-exact dry; mix=1 matches `std::sin`; +monotonic/odd/clamp/stateless tests |
| `test_DelayLine` | impulse bit-exact at integer delay; fractional delay matches (1−f, f) to 1e−6; feedback cascade bit-exact fb^n; mix bit-exact; setDelay(0) bypass verified; delay/fb/mix clamp; +stability. Impl fixes: (1) sub-sample delay bypasses stale-slot read, (2) `readDelay` uses int-index + small-magnitude fraction to avoid ULP loss |
| `test_TapeDelay` | dry-mix bit-exact; pre-delay silence < 1e−20 (Biquad anti-denormal at 1e−25); impulse window peak from RBJ coef analytics; dark-vs-bright RMS strictly less with 2× margin; \|out\| ≤ 1 + ULP via tanh bound; +clamped-wow, 10 s stability. Impl fixes: `readDelay` int-index + small-fraction; tape coloring moved to playback path so first echo is filtered |
| `test_FeedbackBuffer` | stopped/play/overdub integer-position bit-exact; lerp tol 1e−7 from float ULP; mix/feedback bit-exact; speed clamp bit-exact; crossfade smoothness < 0.1 jump for any signal; +10 s stability. Impl fix: `readSample` off-by-(cf−1) wrap replaced by head-tail blend with effective playback length = loopLength − cf |
| `test_Compressor` | DC steady-state gainDb matches −(L−T)(1 − 1/R) to 0.01 dB; below-threshold bit-exact; makeup gain ratio 10^(dB/20); sine peak bounded by 10^(gr/20)·[0.7, 1.5] ripple envelope; release/attack ratio ≥ 10×; +stability |
| `test_PeakLimiter` | below-threshold bit-exact; overshoot bound 2e−3 (≈ 10× sim-observed, under 1/α_r^N worst case); gain recovery exp(−10) residual at 10τ; lookahead delay bit-exact at N−1; threshold/lookahead clamp; +stability |
| `test_Reverb` | energy decay ratio 0.9; wet level; DC offset 0.01 |
| `test_HallReverb` | pre-delay accuracy; modulation depth |
| `test_ShimmerReverb` | octave content ratio 0.02; energy bounds ±6.0; DC 0.05 |
| `test_FMSynth` | bounds bit-exact via hard-limit; single-carrier RMS = 0.85/√2 ≈ 0.6; velocity ratio bit-exact 10/3; pitch ratio from 2·f·Δt crossings (±2 boundary); distinct-routing threshold from (1 − J₀(M)) RMS floor; +10 s stability across all 8 algos and extreme-params combo |
| `test_Voice` | bounds bit-exact via polyBLEP saw ∈ [−1, 1]; produces-sound RMS = 1/√3 · env_rms ≈ 0.48; velocity ratio bit-exact 10/3; sources RMS ≈ 0.575 each (1/√3); filter ratio from 2nd-order Butterworth \|H\|² = 1 / (1 + (f/fc)⁴) giving ~0.16 expected, threshold 0.5; +10 s stability across source × filter and high-Q extreme-combo |
| `test_Polyphony` | silent/reset bit-exact via tanh(0)=0; single-voice RMS = ⟨tanh²(saw)⟩ = 1 − tanh(1) ≈ 0.238; chord/single ratio from CLT Gaussian σ = 1 giving ≈ 1.29 (threshold 1.1); rail threshold from arctanh(1 − ULP/2) ≈ 8.7; +explicit steal-oldest semantic test; +10 s stability with resonant filter |
| `test_SeqClick` | Voice-vs-offline RMS tol from BitCrusher 32-bit quantisation (≈ 6e−8) vs wrong-order divergence (~0.1); click-delta 0.02 from envAttack ≈ 0.107 follower response: clean ramp Δ_env ≈ 1e−3, hard click ≈ 0.037, SVFilter-ring ≈ 4e−3–7e−3 per Q/drive case |
| `test_AudioSafety` | denormal bounds; long-run stability |

## Unaudited

_(Add a row here when a new test file is committed. Move it to the table above
once all thresholds in the file have a stated derivation.)_

- `test_KarplusStrong` — KS loop math (g_raw = 10^(-3/N_cyc), N_cyc = decay·freq);
  silence ε from DelayLine 1e-25 anti-denormal; exciter burst sample count =
  min(kExciterSec·sr, D−1); auto-correlation peak at lag D = sr/freq for pitch
  verification (replaced earlier ZC-counting variant which was invalid for
  broadband KS output); HF energy 6 dB drop bound for damping; -60 dB ratio
  bracket [0.5e-3, 5e-3] for decay accuracy; loop gain kMaxLoopGain = 0.9995
  hard ceiling for stability. To audit and graduate.
- `test_ModalResonator` — Per-partial Q = π·fc·decay/ln(1000) (closed-form
  2nd-order resonator T60 → Q mapping); Goertzel single-bin magnitude for
  partial-presence assertions; BP off-bin attenuation ≈ (f/fc − fc/f)²·Q² for
  partial-isolation; 1e-5 RMS floor at t=0.5s for long-decay (T60=2s) audibility;
  10s post-strike stability; default kMinQ=1 / kMaxQ=5000 clamps prevent
  degenerate / numerically unhelpful Q. To audit and graduate.
- `test_GranularProcessor` — derivations are stated inline (Hann mean = 0.5;
  gain-comp 1/√(O·0.5) with O = grainRate·grainSize; Poisson grain-count
  variance for pile-up bounds; Goertzel-bin power for pitch-passthrough);
  promote to the audited table once a second pair of eyes signs off.
