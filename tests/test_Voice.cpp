#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Voice.h>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

// Threshold derivations used throughout this file
// -----------------------------------------------
// Voice signal chain (src/Voice.cpp::process):
//     freq = portamento → LFO pitch-mod (if depth > 0)
//     sample = source(freq)          (Oscillator / Wavetable / Noise)
//     sample = filter(sample)        (if filterType != Off)
//     sample *= env · velocity       (VCA)
//     sample = bitCrusher(sample)    (passthrough at default settings)
//
// Sources (all produce |sample| ≤ 1):
//   - Oscillator saw: 2·phase − 1 with polyBLEP correction.  Band-limited
//     saw stays in (−1, 1): near the 0/1 wrap the polyBLEP subtracts the
//     discontinuity so the corrected value ramps smoothly from +1−2dt to
//     0 to −1+2dt across a 2·dt window (computed from the formula in
//     src/Oscillator.cpp::polyblep).  Elsewhere 2·phase−1 ∈ (−1, 1).
//     Theoretical RMS of an ideal saw = 1/√3 ≈ 0.577.
//   - Wavetable::sawTable(): linear ramp −1 → +1 across the table, same
//     1/√3 RMS.
//   - Noise (xorshift32): uniform [−1, 1], σ = 1/√3, so RMS = 1/√3.
//
// AdsrEnvelope (see test_Envelope.cpp for full derivations):
//   setAttack(τ) → linear ramp 0 → 1 over τ·sr samples.
//   setSustain(L) + default decay=0 → after Attack, level snaps to L
//     (Decay branch snaps when diff < 1e−4 and decayCoef = 0 gives
//     diff·0 = 0 on first decay sample).
//   setDecay(τ_d) exponential toward sustain with coef exp(−6.9078/(τ_d·sr)).
//
// Velocity is the very last non-crusher multiplier, so two Voice
// instances that share every other parameter produce outputs that are
// exact scalar multiples of each other (bit-for-bit up to the ULP of
// the final float-multiplication step).
//
// Filter is SVFilter (TPT, Cytomic) — see src/SVFilter.cpp.  The public
// Voice::setFilter(type, fc, q) maps the Biquad-style Q input to
// SVFilter resonance via the REPL reference mapping
//     res = (1 − 0.707 / max(q, 0.707)) · 0.9
// so q = 0.707 → res = 0 (k = 2, Q_svf = 1/k = 0.5, overdamped),
// q = 2.0   → res ≈ 0.582 (k ≈ 0.836, Q_svf ≈ 1.20),
// q = 8.0   → res ≈ 0.821 (k ≈ 0.359, Q_svf ≈ 2.79),
// q → ∞     → res = 0.9  (k = 0.2, Q_svf = 5, cap).
// SVFilter LP transfer magnitude:
//     |H_LP(f/fc)|² = 1 / ((1 − (f/fc)²)² + (f/(fc·Q_svf))²)
// Used in the "filter affects timbre" derivation below.

static float rms(const float* buf, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += static_cast<double>(buf[i]) * static_cast<double>(buf[i]);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(n)));
}

TEST_CASE("Voice: output is in [-1, 1] range", "[voice]")
{
    // Default source = Oscillator saw, no filter.  Output = saw · env ·
    // velocity with |saw| ≤ 1 (polyBLEP-corrected), env ∈ [0, 1],
    // velocity = 1, BitCrusher default = passthrough.  So |output| ≤ 1
    // bit-exactly at these settings (no tolerance needed).
    ideath::Voice v;
    v.prepare(kSampleRate);
    v.setAttack(0.001f);
    v.setDecay(0.01f);
    v.setSustain(0.8f);
    v.setRelease(0.01f);

    v.noteOn(440.0f);

    constexpr int N = 4410;
    for (int i = 0; i < N; ++i)
    {
        float s = v.process();
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
    }
}

TEST_CASE("Voice: produces sound after noteOn", "[voice]")
{
    // Default source = saw (RMS = 1/√3 ≈ 0.577), velocity = 1.
    // Envelope: attack 1 ms = 44 samples linear ramp, then decay with
    // τ = 0.1 s toward sustain = 0.8.  Over N = 4410 samples (0.1 s =
    // 10× attack + 1× decay time):
    //   <env²>_attack ≈ (1/3)·(44/4410) ≈ 3.3e−3            (linear 0→1)
    //   <env²>_decay  ≈ 0.64 + 0.32·<e^(−6.9·n/4410)> + 0.04·<e^(−13.8·n/4410)>
    //                 ≈ 0.64 + 0.32·0.146 + 0.04·0.073
    //                 ≈ 0.69      over 4366 / 4410 of the window
    //   <env²>        ≈ 0.686    ⇒ RMS_env ≈ 0.828
    //   RMS_output    ≈ 1/√3 · 0.828 ≈ 0.478
    // Threshold > 0.4 leaves ~15 % headroom; < 0.55 rejects a regression
    // that stops filtering/shaping the source.
    ideath::Voice v;
    v.prepare(kSampleRate);
    v.setAttack(0.001f);
    v.setDecay(0.1f);
    v.setSustain(0.8f);
    v.setRelease(0.1f);

    v.noteOn(440.0f);

    constexpr int N = 4410;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i)
        buf[static_cast<size_t>(i)] = v.process();

    const float r = rms(buf.data(), N);
    REQUIRE(r > 0.4f);
    REQUIRE(r < 0.55f);
}

TEST_CASE("Voice: silent when idle (no noteOn)", "[voice]")
{
    // Derivation: env_.isActive() == false (Stage::Idle) → Voice::process
    // returns literal 0.0f on the early-out branch.  Bit-exact; the 1e−9
    // tolerance is a no-op for the float-matcher API.
    ideath::Voice v;
    v.prepare(kSampleRate);

    constexpr int N = 1000;
    for (int i = 0; i < N; ++i)
        REQUIRE_THAT(v.process(), WithinAbs(0.0f, 1e-9f));
}

TEST_CASE("Voice: isActive reflects envelope state", "[voice]")
{
    // Binary behaviour check.  Release = 5 ms → envelope reaches the
    // 1e−5 flush-to-zero threshold after ~10× τ = 50 ms = 2205 samples,
    // so 44100 samples (1 s, 20× margin) is more than enough for the
    // AdsrEnvelope to return to Idle.
    ideath::Voice v;
    v.prepare(kSampleRate);
    v.setAttack(0.001f);
    v.setDecay(0.01f);
    v.setSustain(0.8f);
    v.setRelease(0.005f);

    REQUIRE_FALSE(v.isActive());

    v.noteOn(440.0f);
    REQUIRE(v.isActive());

    for (int i = 0; i < 1000; ++i)
        v.process();
    REQUIRE(v.isActive());

    v.noteOff();
    REQUIRE(v.isActive());

    for (int i = 0; i < 44100; ++i)
        v.process();
    REQUIRE_FALSE(v.isActive());
}

TEST_CASE("Voice: velocity scales output", "[voice]")
{
    // Velocity is a post-mixing scalar multiplier applied in
    //     sample *= envVal * velocity_
    // so two Voice instances that share every other parameter (source,
    // oscillator phase after reset() = 0, envelope trajectory, default
    // filter off, default BitCrusher passthrough) produce
    //     sample_loud[i] = (1.0/0.3) · sample_quiet[i]
    // bit-for-bit up to one float-multiplication ULP per sample.  The
    // RMS integration accumulates √N · ULP ≈ 7e−6 relative; tolerance
    // 1e−4 covers that with ~15× margin.
    ideath::Voice loud;
    loud.prepare(kSampleRate);
    loud.setAttack(0.001f);
    loud.setSustain(1.0f);
    loud.noteOn(440.0f, 1.0f);

    ideath::Voice quiet;
    quiet.prepare(kSampleRate);
    quiet.setAttack(0.001f);
    quiet.setSustain(1.0f);
    quiet.noteOn(440.0f, 0.3f);

    constexpr int N = 4410;
    std::vector<float> bufLoud(N), bufQuiet(N);
    for (int i = 0; i < N; ++i)
    {
        bufLoud[static_cast<size_t>(i)] = loud.process();
        bufQuiet[static_cast<size_t>(i)] = quiet.process();
    }

    const float rL = rms(bufLoud.data(), N);
    const float rQ = rms(bufQuiet.data(), N);
    REQUIRE(rQ > 0.0f);
    REQUIRE_THAT(rL / rQ, WithinAbs(10.0f / 3.0f, 1e-4f));
}

TEST_CASE("Voice: reset returns to idle", "[voice]")
{
    // Binary + bit-exact: reset() clears env_ to Stage::Idle, so the
    // next process() takes the early-out branch and returns literal
    // 0.0f.
    ideath::Voice v;
    v.prepare(kSampleRate);
    v.noteOn(440.0f);
    for (int i = 0; i < 100; ++i)
        v.process();

    REQUIRE(v.isActive());
    v.reset();
    REQUIRE_FALSE(v.isActive());
    REQUIRE_THAT(v.process(), WithinAbs(0.0f, 1e-9f));
}

TEST_CASE("Voice: different sources produce output", "[voice]")
{
    // All three sources have theoretical RMS = 1/√3 ≈ 0.577:
    //   Oscillator saw  — band-limited 2·phase − 1
    //   Wavetable saw   — linear ramp table, same waveshape
    //   Noise           — uniform [−1, 1], σ = 1/√3
    // Envelope reaches level 1 after 44-sample attack (default decay=0
    // snaps Decay→Sustain immediately when sustain = 1.0), so at N =
    // 4410 samples <env²> ≈ 0.993 (see derivation header) and RMS_env
    // ≈ 0.997.  Expected RMS ≈ 0.577 · 0.997 ≈ 0.575 per source.
    // Threshold > 0.4 leaves ~30 % margin and rejects a regression that
    // silences a source.
    for (auto src : {ideath::Voice::Source::Oscillator,
                     ideath::Voice::Source::Wavetable,
                     ideath::Voice::Source::Noise})
    {
        ideath::Voice v;
        v.prepare(kSampleRate);
        v.setSource(src);
        v.setAttack(0.001f);
        v.setSustain(1.0f);
        v.noteOn(440.0f);

        constexpr int N = 4410;
        std::vector<float> buf(N);
        for (int i = 0; i < N; ++i)
            buf[static_cast<size_t>(i)] = v.process();

        INFO("source = " << static_cast<int>(src));
        REQUIRE(rms(buf.data(), N) > 0.4f);
    }
}

TEST_CASE("Voice: filter affects timbre", "[voice]")
{
    // Saw at f₀ = 440 Hz through SVFilter LP at fc = 200 Hz, q = 0.707.
    // The Q→resonance mapping at q = 0.707 gives res = 0, so k = 2 and
    // Q_svf = 0.5 (overdamped — more attenuation than RBJ Butterworth).
    //
    // SVFilter LP: |H(f/fc)|² = 1 / ((1 − (f/fc)²)² + (f/(fc·Q_svf))²).
    // Saw harmonic amplitudes: a_n = 2/(nπ), harmonic RMS² = a_n²/2.
    // Per-harmonic response at f_n = n·440, f/fc = 2.2·n:
    //   n=1 (440 Hz, f/fc = 2.2): |H|² = 1/(14.75 + 19.36) = 0.0293
    //   n=2 (880 Hz, f/fc = 4.4): |H|² = 1/(336.8 + 77.4) = 0.00241
    //   n=3 (1320 Hz, f/fc = 6.6): |H|² = 1/(1811 + 174) ≈ 5.0e−4
    // Filtered RMS² = 0.2026·0.0293 + 0.0507·0.00241 + 0.0225·5e−4 + …
    //              ≈ 0.00594 + 1.22e−4 + 1.13e−5 + … ≈ 6.07e−3
    //              ⇒ RMS_filt ≈ 0.078
    // Unfiltered RMS² = 1/3 ⇒ RMS_unfilt ≈ 0.577.
    //
    // Expected ratio RMS_filt / RMS_unfilt ≈ 0.135 (~ −17.4 dB).
    // Threshold < 0.5 gives 3.7× headroom above the expected ratio and
    // catches any regression that leaves the filter bypassed.
    ideath::Voice unfiltered;
    unfiltered.prepare(kSampleRate);
    unfiltered.setAttack(0.001f);
    unfiltered.setSustain(1.0f);
    unfiltered.noteOn(440.0f);

    ideath::Voice filtered;
    filtered.prepare(kSampleRate);
    filtered.setAttack(0.001f);
    filtered.setSustain(1.0f);
    filtered.setFilter(ideath::Voice::FilterType::Lowpass, 200.0f, 0.707f);
    filtered.noteOn(440.0f);

    constexpr int N = 4410;
    std::vector<float> bufU(N), bufF(N);
    for (int i = 0; i < N; ++i)
    {
        bufU[static_cast<size_t>(i)] = unfiltered.process();
        bufF[static_cast<size_t>(i)] = filtered.process();
    }

    const float rU = rms(bufU.data(), N);
    const float rF = rms(bufF.data(), N);
    REQUIRE(rU > 0.4f);          // sanity: unfiltered saw is loud
    REQUIRE(rF < rU * 0.5f);     // -6 dB lower bound, expected ~ -16 dB
}

TEST_CASE("Voice: portamento glides frequency", "[voice]")
{
    // Smoke test: a retrigger with an active portamento must not
    // deactivate the voice.  Per-sample portamento behaviour is covered
    // in test_Portamento.cpp; here we only verify Voice wiring — that
    // noteOn(newFreq) during an active note calls porta_.setTarget and
    // env_.noteOn (which enters Retrigger → Attack) rather than
    // producing a hard jump or killing the envelope.
    ideath::Voice v;
    v.prepare(kSampleRate);
    v.setAttack(0.001f);
    v.setSustain(1.0f);
    v.setPortamento(0.1f);

    v.noteOn(220.0f);
    for (int i = 0; i < 2205; ++i)
        v.process();

    v.noteOn(880.0f);
    REQUIRE(v.isActive());

    // After processing a short window the voice must still be active —
    // env is in Attack or Retrigger, not Idle.
    for (int i = 0; i < 100; ++i)
    {
        const float s = v.process();
        REQUIRE(std::isfinite(s));
    }
    REQUIRE(v.isActive());
}

TEST_CASE("Voice: 10-second stability across sources and filter types", "[voice]")
{
    // CLAUDE.md testing convention: "primitives with feedback or phase
    // accumulators must be tested for at least 10 seconds of continuous
    // processing to catch precision drift and denormal accumulation".
    // Voice contains: oscillator phase accumulator, SVFilter integrator
    // state (ic1eq_, ic2eq_), LFO phase, Portamento exp state, envelope
    // state.
    //
    // Drive each source × a non-trivial filter configuration for 10 s
    // and verify output stays finite and bounded within a primitive-
    // appropriate range.  q = 2 maps to res ≈ 0.582 → Q_svf ≈ 1.20.
    // Per the SVFilter ceiling table (|s| ≤ Q_svf steady-state, up to
    // ~2·Q_svf during modulation transients), the 10-s bound |s| ≤ 2.5
    // sits just above that (2.5 / 1.20 ≈ 2.08×).  Bound survives both
    // Biquad (pre-migration, ±q = ±2) and SVFilter topologies.
    struct Config {
        ideath::Voice::Source src;
        ideath::Voice::FilterType filterType;
        float fc;
    };
    const Config configs[] = {
        { ideath::Voice::Source::Oscillator, ideath::Voice::FilterType::Lowpass,  800.0f },
        { ideath::Voice::Source::Wavetable,  ideath::Voice::FilterType::Highpass, 200.0f },
        { ideath::Voice::Source::Noise,      ideath::Voice::FilterType::Bandpass, 1000.0f },
    };

    for (const auto& cfg : configs)
    {
        ideath::Voice v;
        v.prepare(kSampleRate);
        v.setSource(cfg.src);
        v.setAttack(0.001f);
        v.setDecay(0.5f);
        v.setSustain(0.8f);
        v.setRelease(0.1f);
        v.setFilter(cfg.filterType, cfg.fc, 2.0f);
        v.noteOn(440.0f);

        constexpr int N = 441000; // 10 s
        bool allFinite = true;
        bool allBounded = true;
        for (int i = 0; i < N; ++i)
        {
            const float s = v.process();
            if (!std::isfinite(s)) { allFinite = false; break; }
            if (s < -2.5f || s > 2.5f) { allBounded = false; break; }
        }
        INFO("source=" << static_cast<int>(cfg.src)
             << " filter=" << static_cast<int>(cfg.filterType));
        REQUIRE(allFinite);
        REQUIRE(allBounded);
    }
}

TEST_CASE("Voice: stable under per-sample cutoff modulation at high Q", "[voice]")
{
    // Regression guard for filter modulation stability.  SVFilter (TPT /
    // Cytomic) is modulation-safe by construction: rapid per-sample
    // coefficient changes do not inject state transients beyond the
    // steady-state resonant peak, because the trapezoidal integration
    // updates state consistently with the new coefficients each sample.
    // Biquad DF-II-Transposed is not formally modulation-safe, though at
    // moderate mod rates (20 Hz here) and q = 8 it happens to stay
    // bounded — so this test passes under both topologies.  It fails if
    // a future refactor replaces the filter with something genuinely
    // unsafe (e.g. Direct Form I at high Q) or loses the q clamping.
    //
    // Sweeps cutoff sinusoidally at 20 Hz across 200 Hz – 4 kHz
    // (per-sample setFilter() calls) on a saw source, q = 8 → Q_svf ≈
    // 2.79 via the REPL reference mapping.
    //
    // Rationale for the ±6 bound: Q_svf = 2.79 is the steady-state
    // resonant peak.  Cytomic TPT's state-update invariant bounds
    // modulation transients at ~2·Q_svf ≈ 5.58 (see the SVFilter test
    // "stable under fast cutoff modulation", which uses Q_svf = 2.5 and
    // a 2·Q bound).  ±6 adds 0.42 margin on top.
    ideath::Voice v;
    v.prepare(kSampleRate);
    v.setAttack(0.001f);
    v.setSustain(1.0f);
    v.setRelease(0.1f);
    v.noteOn(440.0f, 1.0f);

    constexpr int N = 44100; // 1 s
    bool allFinite = true;
    bool allBounded = true;
    for (int i = 0; i < N; ++i)
    {
        // 20 Hz cutoff LFO between 200 Hz and 4000 Hz.
        const float phase = 2.0f * static_cast<float>(M_PI) * 20.0f
                          * static_cast<float>(i) / kSampleRate;
        const float fc = 200.0f + 3800.0f * (0.5f + 0.5f * std::sin(phase));
        v.setFilter(ideath::Voice::FilterType::Lowpass, fc, 8.0f);

        const float s = v.process();
        if (!std::isfinite(s)) { allFinite = false; break; }
        if (s < -6.0f || s > 6.0f) { allBounded = false; break; }
    }
    REQUIRE(allFinite);
    REQUIRE(allBounded);
}

TEST_CASE("Voice: extreme parameter combination stays finite", "[voice]")
{
    // CLAUDE.md: "test pairs or triples of extreme parameters together".
    // Combine high-Q resonant filter × saw (harmonic-rich source) × LFO
    // pitch modulation × BitCrusher × full velocity.  q = 8 maps to
    // res ≈ 0.821 → Q_svf ≈ 2.79, so SVFilter steady-state peak is
    // ~2.79 on a tone at resonance, with transients up to ~2× that
    // under LFO + BitCrusher jitter.  We allow ±10 as a finite-signal
    // sanity bound (no runaway, no NaN).  The primitive contract does
    // not clamp output — the plugin layer places a PeakLimiter
    // downstream.  Bound survives both Biquad (Q_biquad = 8) and
    // SVFilter topologies.
    ideath::Voice v;
    v.prepare(kSampleRate);
    v.setAttack(0.001f);
    v.setDecay(0.1f);
    v.setSustain(1.0f);
    v.setRelease(0.1f);
    v.setFilter(ideath::Voice::FilterType::Lowpass, 440.0f, 8.0f); // cutoff = fundamental
    v.setLfoRate(4.0f);
    v.setLfoPitchDepth(2.0f); // ±2 semitones
    v.setBitDepth(6);
    v.noteOn(440.0f, 1.0f);

    constexpr int N = 44100; // 1 s
    bool allFinite = true;
    bool allBounded = true;
    for (int i = 0; i < N; ++i)
    {
        const float s = v.process();
        if (!std::isfinite(s)) { allFinite = false; break; }
        if (s < -10.0f || s > 10.0f) { allBounded = false; break; }
    }
    REQUIRE(allFinite);
    REQUIRE(allBounded);
}
