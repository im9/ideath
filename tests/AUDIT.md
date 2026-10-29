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
| `test_TapeDelay` | dry-mix bit-exact; pre-delay silence < 1e−20 (Biquad anti-denormal at 1e−25); impulse window peak from RBJ coef analytics; dark-vs-bright RMS strictly less with 2× margin; \|out\| ≤ 1 + ULP via tanh bound; +clamped-wow, 10 s stability. Impl fixes: `readDelay` int-index + small-fraction; tape coloring moved to playback path so first echo is filtered. Stereo / clean / diffusion: offset echo position ±1 sample (filter group delay); Offset L bit-identical to mono; ping-pong silent side ≤ −60 dB (20 Hz HP ring e⁻⁹ after 100 ms); saturation-off linearity 1e−5 (float rounding); 8 kHz loss over three passes at fraction 0.5 ≥ −3 dB (half power; linear read −4.5 dB, Hermite −1 dB); min delay 2 samples (Hermite); wow/flutter high-band artefacts (above 4·f + 4 kHz) ≤ −80 dB re total (16-bit noise re this −10.5 dBFS tone ≈ −87.5 dB; a first-order allpass read measured −66 dB); diffusion 0 click ≥ 99 % in ±5 samples, diffusion 1 < 50 %; echo energy centroid within 1 ms (allpass mean group delay = its length; 2 ms temporal-order threshold); echo-to-echo ratio within 1 dB (lossless allpass); delay swept 9.5 → 11.5 ms across the diffuser threshold, and diffusion switched 0 → 0.7 → 0 mid-stream: largest step ≤ 2 × the clean 200 Hz signal's own steepest step (0.6 · 2π · 200 / 44 100 = 0.0171), no 10 ms window below half the steady RMS (lossless allpasses: a dropout reads far lower) |
| `test_FeedbackBuffer` | stopped/play/overdub integer-position bit-exact; lerp tol 1e−7 from float ULP; mix/feedback bit-exact; speed clamp bit-exact; crossfade smoothness < 0.1 jump for any signal; +10 s stability. Impl fix: `readSample` off-by-(cf−1) wrap replaced by head-tail blend with effective playback length = loopLength − cf |
| `test_Compressor` | DC steady-state gainDb matches −(L−T)(1 − 1/R) to 0.01 dB; below-threshold bit-exact; makeup gain ratio 10^(dB/20); sine peak bounded by 10^(gr/20)·[0.7, 1.5] ripple envelope; release/attack ratio ≥ 10×; +stability |
| `test_PeakLimiter` | below-threshold bit-exact; overshoot bound 2e−3 (≈ 10× sim-observed, under 1/α_r^N worst case); gain recovery exp(−10) residual at 10τ; lookahead delay bit-exact at N−1; threshold/lookahead clamp; +stability |
| `test_Reverb` | energy decay ratio 0.9; wet level; DC offset 0.01; freeze hold 0.95 over 1 s blocks (lossless combs, partial-period bound L/N = 1640/44100 ≈ 3.7 %); input during freeze bit-exact no-op |
| `test_ReverbTank` | T60 tolerance 10 % = 2 × the ≈ 5 % reverberation-time JND (ISO 3382-1); level tolerance 1 dB = broadband level JND; T60 by noise burst + band filter + T30, mean of 8 seeds (single burst σ ≈ 5 % at 0.5 s; a sine burst excites 1–2 modes and scattered −23…+65 %); 4 kHz damp check in a 1/6-octave band (absorption spans 1.73 → 1.05 s across a 2/3-octave band); spec T60(4 kHz) = 60/(60/T60 + 60·damp); steady-state 0 dB at kUnityT60 after 0.6·T60 (residual 10^−3.6); L/R correlation ≤ 0.3 (Beranek 1 − IACC ≥ 0.7); sustained tones C2–C6 balanced within ±3 dB (3 dB ILD ≈ 10° image shift); freeze 10 s within +1 / −3 dB (JND / half power); reset bit-identical; no subnormals in a 10 s tail |
| `test_HallReverb` | size map 0.5·60^size exact; T60 monotone over 21 steps; 250 Hz T60 within 10 % of the map (seed-averaged); damp shortens 6 kHz more than 250 Hz, 250 Hz within 10 %; pre-delay silence < 1e−20 (anti-denormal residue) and onset ≤ 50 ms after (diffuser span ≈ 30 ms); pre-delay keeps its time across prepare() at a new rate; modulation difference energy ≥ −10 dB of the tail; freeze +1/−3 dB; input during freeze bit-exact no-op; mix 0 bit-exact; extreme settings ≤ kMaxOutput 35 (header derivation); hidden `[.t60table]` prints the size → T60 table |
| `test_ShimmerReverb` | pitch ±10 cents (5–10 cent pitch JND) for +12, +19, +7, +24, −12, −24, +5.5 and voice 2; amount 0 → octave band ≤ −80 dB (Hermite/float floor ≈ −140 dB); Input mode 880/440 ≤ −30 dB (upward masking ≈ −25 dB at one octave), Feedback ≥ −10 dB (nominal −3, 7 dB for AA/normaliser/band edges), band-noise stimulus (a sine lands on one mode, ±10 dB); unshifted 220 Hz T60 within 10 % across amount 0 / 0.5 / 1 in both modes; LF T60 follows the Hall size map; freeze 10 s +1/−3 dB; freeze level step −1.05 ± 1 dB (half a 0.2 s window of free decay); generation bands do not grow while frozen (1 dB, mean of 6 seeds: single-burst band σ ≈ 0.8 dB from modal beating); Input-mode note length: 50 ms … 3 s two-octave band-noise bursts at +12 / +24 (220 Hz source) and −12 / −24 (1760 Hz source), shifted/source energy minus a no-shift tank control in the same two bands (the tank's band gain is not flat: 220 vs 880 Hz differs by 1.8 dB on 50 ms bursts) within ±1.5 dB (1 dB JND + 0.36 dB low cut at the −24 band's lower edge, rounded); 12 onsets spread over one grain period after a 1 s warm-up — a short note's level depends on the grain phase it lands on; wet level vs Hall within 1 dB on pink noise across size × damp × mode × amount × voices; extreme settings ≤ kMaxOutput; mod depth detune ≥ 4 cents (half the 8-cent swing, 4-cent bins); reset bit-identical / silent; no subnormals |
| `test_FMSynth` | bounds bit-exact via hard-limit; single-carrier RMS = 0.85/√2 ≈ 0.6; velocity ratio bit-exact 10/3; pitch ratio from 2·f·Δt crossings (±2 boundary); distinct-routing threshold from (1 − J₀(M)) RMS floor; +10 s stability across all 8 algos and extreme-params combo |
| `test_Voice` | bounds bit-exact via polyBLEP saw ∈ [−1, 1]; produces-sound RMS = 1/√3 · env_rms ≈ 0.48; velocity ratio bit-exact 10/3; sources RMS ≈ 0.575 each (1/√3); filter ratio from 2nd-order Butterworth \|H\|² = 1 / (1 + (f/fc)⁴) giving ~0.16 expected, threshold 0.5; +10 s stability across source × filter and high-Q extreme-combo |
| `test_Polyphony` | silent/reset bit-exact via tanh(0)=0; single-voice RMS = ⟨tanh²(saw)⟩ = 1 − tanh(1) ≈ 0.238; chord/single ratio from CLT Gaussian σ = 1 giving ≈ 1.29 (threshold 1.1); rail threshold from arctanh(1 − ULP/2) ≈ 8.7; +explicit steal-oldest semantic test; +10 s stability with resonant filter |
| `test_SeqClick` | Voice-vs-offline RMS tol from BitCrusher 32-bit quantisation (≈ 6e−8) vs wrong-order divergence (~0.1); click-delta 0.02 from envAttack ≈ 0.107 follower response: clean ramp Δ_env ≈ 1e−3, hard click ≈ 0.037, SVFilter-ring ≈ 4e−3–7e−3 per Q/drive case |
| `test_AudioSafety` | denormal bounds; long-run stability |
| `test_KarplusStrong` | KS loop math (g_raw = 10^(-3/N_cyc), N_cyc = decay·freq); silence ε ≤ 1e-10 from DelayLine 1e-25 anti-denormal; exciter burst clamped to min(kExciterSec·sr, D−1) so it never overlaps a delay-line slot → ±1.001 bound; autocorrelation peak at lag D = sr/freq with ±1-sample tolerance for sample-rate-independent pitch; HF energy 6 dB drop bound for damping; −60 dB envelope ratio bracket [0.5e−3, 5e−3] for decay accuracy; loop gain kMaxLoopGain = 0.9995 strict ceiling; damping=1 → 0.3× peak ratio (10 dB) catches "damping ignored"; minDecay 0.05 s → 0.1× ratio (20 dB) catches "decay ignored"; setFrequency mid-pluck re-clamps remaining burst |
| `test_ModalResonator` | Per-partial Q = π·fc·decay/ln(1000) from closed-form 2nd-order resonator T60↔Q mapping; per-partial BP × Q at output sum → per-partial peak ≈ 1; output ceiling N·1.5 (≈ N partials in phase × 2nd-order transient overshoot factor); BP off-bin attenuation (Δf/BW)²·Q² with 5× threshold margin for Goertzel partial-isolation; inharmonicity stretch f_i = ratio_i·√(1+B·ratio_i²), B = inharm·0.1; long-decay (T60=2 s) audibility floor 1e-5 RMS = −100 dBFS above denormal floor; long/short decay ratio 10× from exp(−6.9·t/T60); kMinQ=1 / kMaxQ=5000 clamps |
| `test_GranularProcessor` | Universal worst-case output bound y_max ≤ kMaxGrains · Hann_peak · gain_comp(O) · \|input\|max, with gain_comp(O) = 1/√max(O·0.5, 0.5); Hann mean 0.5 from ∫₀¹(0.5−0.5·cos 2πt)dt; gain-comp's 0.5 floor caps boost at √2 for sparse-grain regimes; bit-exact zero on no-grains state and frozen-buffer DC sign-band; pitch-passthrough 4× ratio for octave-failure detection from Hann main-lobe leakage; CLT-based ±0.4 (≈ 28%) tolerance on √2 expectation at O=4; pool-saturation floor (1.0 bound at O=2000 with gain_comp ≈ 0.032) |

## Unaudited

_(Add a row here when a new test file is committed. Move it to the table above
once all thresholds in the file have a stated derivation.)_

| Test file | Status |
|---|---|
| `test_HarmonicOscillator` | New file; thresholds carry derivations inline (Goertzel amplitude tolerance ±0.01 from N=44100 bin-aligned leakage floor; ±N peak ceiling from N partials in phase; HIGH-fundamental Nyquist bound from `sr × 0.45 = 19845 Hz`; shape=1 linear-taper expected values 1.0 / 0.5 / 0.0 for LOW band derived from `band_pos × (width-1)`). Pending full audit pass to graduate to main table. |
| `test_BowedString` | New file; thresholds carry derivations inline (silence floor 1e-10 from DelayLine +1e-25 anti-denormal × loop circulation, matching KarplusStrong; ±2 output bound from triangle inequality `|tanh|+|tanh| ≤ 2`; comb-notch 10× separation from `\|H(n)\|=2\|sin(πnp)\|` analytical comb at `p=0.5 → n=2` notch vs `p=0.1 → \|H\|=2sin(0.2π)≈1.18`; ringout 2× factor over 1 s from `loopGain^cycles` at damping-mapped decay seconds; fundamental-vs-non-harmonic 5× margin against friction-driven Helmholtz periodicity). Pending full audit pass to graduate. |
| `test_LowPassGate` | New file (covers both `LowPassGate` and `LowPassGateVoice`); thresholds carry derivations inline (attack threshold 0.9 from `1 − e⁻³ ≈ 0.95` at 3τ × 1 ms = 3 ms with 5% slack; damping-fast/slow 0.5 / 4× factor from `e^(−0.2/0.08) ≈ 0.082` vs `e^(−0.2/0.6) ≈ 0.717` at 200 ms past attack; brightness 10× separation from 1-pole-equivalent attenuation `1/(1 + (f/fc)²)` at f=440 Hz, fc=50 vs 6000 → ratio ≈ 75×; LPGVoice saw-vs-square 5× 2nd-harmonic margin from `2/(π × 2) = 0.318` vs `0` analytical Fourier coefficients; ±1.3 output bound from RBJ LP Q=0.707 peak ≈ +1 dB × ULP). Pending full audit pass to graduate. |
