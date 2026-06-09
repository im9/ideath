#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/KarplusStrong.h>
#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

// ---------------------------------------------------------------------------
// Threshold derivation notes (used throughout this file)
//
// Karplus-Strong loop math (canonical for every test below):
//
//   D       = sampleRate / freq          (delay-line length in samples)
//   N_cyc   = decay * freq               (number of loop cycles in `decay`s)
//   g_raw   = 10^(-3 / N_cyc)            (per-cycle gain → -60 dB at decay)
//   H(f0)   = filter magnitude at pitch
//             = (1-d) / sqrt(1 - 2 d cos ω + d²),  ω = 2π f0 / sr
//   g       = g_raw / H(f0)              (impl's compensated loop gain)
//
// After N cycles (= N×D samples) the envelope is (g·H)^N relative to peak;
// at N = decay·freq cycles this is exactly 10^-3 = -60 dB by construction.
//
// All amplitude thresholds in this file are derived from those identities;
// no threshold is set by "I ran the impl and observed X".
// ---------------------------------------------------------------------------

static constexpr float kSampleRate = 44100.0f;

namespace {

// Compute the peak absolute value over `count` samples starting now.
float peakAbs(ideath::KarplusStrong& ks, int count)
{
    float peak = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        float v = ks.process();
        peak = std::max(peak, std::fabs(v));
    }
    return peak;
}

// Compute RMS over `count` samples.
float rmsRun(ideath::KarplusStrong& ks, int count)
{
    double sum = 0.0;
    for (int i = 0; i < count; ++i)
    {
        float v = ks.process();
        sum += static_cast<double>(v) * static_cast<double>(v);
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

} // namespace

// ---------------------------------------------------------------------------
// 0. Initial state — no allocation, no signal until plucked
// ---------------------------------------------------------------------------

TEST_CASE("KarplusStrong: starts silent and stays silent until plucked", "[karplus]")
{
    ideath::KarplusStrong ks;
    ks.prepare(kSampleRate);
    ks.setFrequency(220.0f);
    ks.setDecay(0.5f);
    ks.setDamping(0.3f);
    ks.setExciter(1.0f);

    // No pluck() → no exciter burst → delay line is zeros → output is
    // zero (modulo DelayLine's anti-denormal 1e-25f DC offset, which is
    // far below any audible / float-precision threshold).
    for (int i = 0; i < 1024; ++i)
    {
        float v = ks.process();
        REQUIRE_THAT(v, WithinAbs(0.0f, 1e-10f));  // 1e-25 → ε, well under 1e-10
    }
    REQUIRE_FALSE(ks.exciterActive());
}

// ---------------------------------------------------------------------------
// 1. Pluck produces signal
// ---------------------------------------------------------------------------

TEST_CASE("KarplusStrong: pluck produces non-zero signal", "[karplus]")
{
    // After a pluck, the loop's first audible output occurs when the noise
    // burst has propagated once through the delay line:
    //   D = sampleRate / freq = 44100 / 440 = ~100.2 samples.
    // We listen for 4 × D = 400 samples → covers four loop cycles, which
    // is far more than enough for any one of the burst's ~44 random
    // samples to surface. Peak threshold 0.1: the noise burst is uniform
    // ±1.0, and the loop gain × filter gain are both close to 1 for one
    // pass, so the loop content's peak should be ≥ 0.5 — 0.1 leaves 5×
    // headroom for unlucky low-magnitude burst draws.
    ideath::KarplusStrong ks;
    ks.prepare(kSampleRate);
    ks.setFrequency(440.0f);   // D ≈ 100 samples — short enough to surface
    ks.setDecay(1.0f);
    ks.setDamping(0.2f);
    ks.setExciter(1.0f);
    ks.pluck();

    float peak = peakAbs(ks, 400);
    REQUIRE(peak > 0.1f);
}

// ---------------------------------------------------------------------------
// 2. Pluck count: exciter is active for exactly kExciterSec * sampleRate
// ---------------------------------------------------------------------------

TEST_CASE("KarplusStrong: exciter burst length matches spec", "[karplus]")
{
    // Spec: kExciterSec = 0.001 s → at 44.1 kHz, max(1, 0.001 * 44100) = 44
    // samples of exciter activity. After 44 process() calls the burst must
    // be exhausted; before that, exciterActive() must remain true.
    ideath::KarplusStrong ks;
    ks.prepare(kSampleRate);
    ks.setFrequency(220.0f);
    ks.setExciter(1.0f);
    ks.pluck();

    const int expected = static_cast<int>(ideath::KarplusStrong::kExciterSec * kSampleRate);
    REQUIRE(ks.exciterActive());
    for (int i = 0; i < expected; ++i)
    {
        REQUIRE(ks.exciterActive());
        ks.process();
    }
    REQUIRE_FALSE(ks.exciterActive());
}

TEST_CASE("KarplusStrong: re-pluck during active burst restarts the count",
          "[karplus]")
{
    // Header contract (KarplusStrong.h on pluck()):
    //   "Calling pluck() again while the previous burst is still in flight
    //    simply replaces the remaining count (a fresh pluck restarts the
    //    burst)."
    //
    // Test: pluck, advance partway through the burst, pluck again, and
    // verify the burst extends for a full kExciterSec window from the
    // *second* pluck — not just the residual of the first.
    //
    // At freq=220, D = 200.45 samples and kExciterSec·sr = 44 samples, so
    // the burst is clamped to min(44, 199) = 44 samples (no overlap).
    // Threshold derivation: burst length is a spec value (kExciterSec·sr),
    // not a derived tolerance.
    ideath::KarplusStrong ks;
    ks.prepare(kSampleRate);
    ks.setFrequency(220.0f);
    ks.setExciter(1.0f);
    ks.pluck();

    const int full = static_cast<int>(ideath::KarplusStrong::kExciterSec * kSampleRate);
    REQUIRE(ks.exciterActive());

    // Advance 5 samples → if "replace" were broken (e.g. cumulative add or
    // no-op), the residual would be 39 or some other non-`full` value.
    for (int i = 0; i < 5; ++i)
        (void)ks.process();
    REQUIRE(ks.exciterActive());

    // Re-pluck. The remaining count must reset to a fresh `full` samples.
    ks.pluck();
    for (int i = 0; i < full; ++i)
    {
        REQUIRE(ks.exciterActive());
        (void)ks.process();
    }
    REQUIRE_FALSE(ks.exciterActive());
}

// ---------------------------------------------------------------------------
// 3. Output bounds — nominal ±1
// ---------------------------------------------------------------------------

TEST_CASE("KarplusStrong: output stays within ±1 nominal range",
          "[karplus][bounds]")
{
    // The exciter is bounded by ±exciterLevel (≤ ±1.0). The loop gain
    // (compensated for filter loss) is clamped to kMaxLoopGain = 0.9995 < 1,
    // so the loop is strictly contractive. The filter is a one-pole LP whose
    // magnitude at any frequency is ≤ 1, so combining the two: at any sample
    // |output| ≤ max(|exciter|, previous loop content) and the loop content
    // monotonically decays. Hence |output| ≤ 1 + a few ULPs.
    //
    // Tolerance 1e-3: float-precision headroom for the multiplicative chain
    // (filter × loopGain × delay-line interpolation) and any in-loop
    // transient. Well below any audible distortion.
    ideath::KarplusStrong ks;
    ks.prepare(kSampleRate);
    ks.setExciter(1.0f);

    for (float freq : { 30.0f, 110.0f, 440.0f, 2000.0f, 5000.0f })
    {
        for (float damp : { 0.0f, 0.3f, 0.7f, 1.0f })
        {
            ks.reset();
            ks.setFrequency(freq);
            ks.setDecay(1.0f);
            ks.setDamping(damp);
            ks.pluck();
            for (int i = 0; i < 22050; ++i)  // 0.5 s per case
            {
                float v = ks.process();
                INFO("freq=" << freq << " damp=" << damp);
                REQUIRE(std::isfinite(v));
                REQUIRE(v >= -1.0f - 1e-3f);
                REQUIRE(v <= 1.0f + 1e-3f);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 4. Pitch correctness — the loop rings at the requested frequency
// ---------------------------------------------------------------------------

TEST_CASE("KarplusStrong: ringing frequency matches setFrequency", "[karplus][pitch]")
{
    // The KS loop length D = sr / freq samples; one full ring cycle = D
    // samples. Auto-correlation of the steady-state tail must peak at lag D.
    // For freq = 441 Hz, sr = 44100 → D = 100 samples exactly. We test that
    // the correlation at lag 100 exceeds the correlation at adjacent lags
    // (50, 150) — a peak, not just any positive value.
    //
    // Threshold: with the exciter only ~1 ms long and the tail being a
    // band-limited (LP-filtered) replica of the burst recirculating, the
    // signal at lag D is a smoothed version of the signal now, so corr(D)
    // should be a clear maximum vs. corr(D/2) or corr(3D/2) which are
    // anti-correlated by half a period of the dominant component.
    ideath::KarplusStrong ks;
    ks.prepare(kSampleRate);
    ks.setFrequency(441.0f);           // D = 100 samples exactly
    ks.setDecay(2.0f);                 // long enough to keep amplitude high
    ks.setDamping(0.1f);
    ks.setExciter(1.0f);
    ks.pluck();

    // Skip the burst (~44 samples) and the first couple of cycles so the
    // loop has settled into its dominant-mode shape.
    for (int i = 0; i < 500; ++i) ks.process();

    std::vector<float> buf(4096);
    for (auto& s : buf) s = ks.process();

    auto corrAt = [&](int lag) {
        double sum = 0.0;
        const int N = static_cast<int>(buf.size()) - lag;
        for (int i = 0; i < N; ++i)
            sum += static_cast<double>(buf[i]) * static_cast<double>(buf[i + lag]);
        return static_cast<float>(sum / static_cast<double>(N));
    };

    const float c50  = corrAt(50);    // half period — should be negative-ish
    const float c100 = corrAt(100);   // full period — should be strong positive
    const float c150 = corrAt(150);   // 1.5 periods — half-period offset

    REQUIRE(c100 > c50);
    REQUIRE(c100 > c150);
    REQUIRE(c100 > 0.0f);
}

// ---------------------------------------------------------------------------
// 5. Decay-time accuracy — -60 dB tail length
// ---------------------------------------------------------------------------

TEST_CASE("KarplusStrong: peak envelope reaches ~-60 dB after `decay` seconds",
          "[karplus][decay]")
{
    // Per the loop math: g_raw = 10^(-3 / N_cyc), N_cyc = decay * freq.
    // The implementation also compensates for filter magnitude, so the
    // *compensated* per-cycle envelope is g * H = g_raw exactly. Over
    // decay seconds, the envelope must drop by 60 dB (10^-3 amplitude).
    //
    // We measure peak amplitude in:
    //   window A: samples [0.05 s, 0.10 s] — just after the exciter, near
    //             the peak of the loop's initial ring-up.
    //   window B: samples [decay - 0.05 s, decay] — at the end of the tail.
    //
    // Expected ratio B/A ≈ 10^(-3 × (window B end / decay)) but since the
    // exciter is at t=0 and we measure the early peak inside window A,
    // window A captures essentially the full peak. So we expect
    //   peakB / peakA ≈ 10^-3 = 1e-3.
    //
    // Tolerance: actual ratio depends on how much of the burst leaks into
    // window A and on the discrete envelope sampling. A factor of 5×
    // tolerance bracket (peakB between 0.5e-3 × peakA and 5e-3 × peakA)
    // is conservative — even a 10% drift in `decay` would still fall
    // inside this bracket. Wider would mask a real bug like "ignored
    // the damping compensation".

    constexpr float decay = 1.0f;
    constexpr float freq  = 220.0f;
    constexpr float damp  = 0.3f;

    ideath::KarplusStrong ks;
    ks.prepare(kSampleRate);
    ks.setFrequency(freq);
    ks.setDecay(decay);
    ks.setDamping(damp);
    ks.setExciter(1.0f);
    ks.pluck();

    // Window A: [0.05 s, 0.10 s]
    for (int i = 0; i < static_cast<int>(0.05f * kSampleRate); ++i)
        ks.process();
    float peakA = peakAbs(ks, static_cast<int>(0.05f * kSampleRate));

    // Skip from 0.10 s to (decay - 0.05 s) = 0.95 s
    const int skip = static_cast<int>((decay - 0.05f - 0.10f) * kSampleRate);
    for (int i = 0; i < skip; ++i) ks.process();

    // Window B: [decay - 0.05 s, decay]
    float peakB = peakAbs(ks, static_cast<int>(0.05f * kSampleRate));

    const float ratio = peakB / std::max(peakA, 1e-12f);
    INFO("peakA=" << peakA << " peakB=" << peakB << " ratio=" << ratio);

    REQUIRE(peakA > 0.01f);             // we actually heard something
    REQUIRE(ratio > 0.5e-3f);           // not faded too fast
    REQUIRE(ratio < 5e-3f);             // not too loud either (would be wrong gain)
}

// ---------------------------------------------------------------------------
// 6. Damping darkens the tail (HF energy decays faster with damping)
// ---------------------------------------------------------------------------

TEST_CASE("KarplusStrong: higher damping reduces high-frequency content",
          "[karplus][damping]")
{
    // Two plucks of the same pitch + decay, differing only in damping.
    // After the same duration the dark version must have less HF energy.
    //
    // We measure the high-pass component by subtracting the LP-smoothed
    // signal from the raw signal: y_hp[n] = x[n] - lp[n], where
    //   lp[n] = lp[n-1] + α * (x[n] - lp[n-1]),  α = 0.05  (one-pole at
    //   roughly fc = α * sr / (2π) ≈ 350 Hz).
    // We test fundamental = 220 Hz, so anything above ~440 Hz (one octave
    // up) is "HF" for the purposes of this test.
    //
    // Threshold: dark must have noticeably less HF energy. We require
    //   rms_hp(dark) < 0.5 × rms_hp(bright)
    // — a 6 dB drop. The damping increase is from 0.1 to 0.85, which gives
    // a much larger drop than 6 dB at the fundamental's harmonics; 0.5 is
    // a robust lower bound for the regression "damping forgotten".
    auto runHF = [](float damping) {
        ideath::KarplusStrong ks;
        ks.prepare(kSampleRate);
        ks.setFrequency(220.0f);
        ks.setDecay(2.0f);
        ks.setDamping(damping);
        ks.setExciter(1.0f);
        ks.pluck();
        // Skip the burst and the first few cycles.
        for (int i = 0; i < 1000; ++i) ks.process();

        constexpr int N = 8192;
        std::vector<float> samples(N);
        for (int i = 0; i < N; ++i) samples[i] = ks.process();

        // One-pole LP, then HP = raw - LP.
        float lp = 0.0f;
        double sumSq = 0.0;
        for (float s : samples)
        {
            lp += 0.05f * (s - lp);
            float hp = s - lp;
            sumSq += static_cast<double>(hp) * static_cast<double>(hp);
        }
        return static_cast<float>(std::sqrt(sumSq / N));
    };

    float hfBright = runHF(0.1f);
    float hfDark   = runHF(0.85f);
    INFO("hfBright=" << hfBright << " hfDark=" << hfDark);
    REQUIRE(hfDark < 0.5f * hfBright);
}

// ---------------------------------------------------------------------------
// 7. Reset clears state
// ---------------------------------------------------------------------------

TEST_CASE("KarplusStrong: reset() returns to silent initial state", "[karplus][reset]")
{
    ideath::KarplusStrong ks;
    ks.prepare(kSampleRate);
    ks.setFrequency(220.0f);
    ks.setDecay(1.0f);
    ks.setDamping(0.3f);
    ks.setExciter(1.0f);
    ks.pluck();

    // Let the loop ring for a while.
    for (int i = 0; i < 1000; ++i) ks.process();

    ks.reset();
    REQUIRE_FALSE(ks.exciterActive());

    // After reset, no further plucks → silence (modulo anti-denormal noise).
    for (int i = 0; i < 512; ++i)
    {
        float v = ks.process();
        REQUIRE_THAT(v, WithinAbs(0.0f, 1e-10f));
    }
}

// ---------------------------------------------------------------------------
// 8. Parameter clamping
// ---------------------------------------------------------------------------

TEST_CASE("KarplusStrong: extreme parameters clamp to safe ranges and stay bounded",
          "[karplus][clamp]")
{
    // Per spec: freq ∈ [kMinFreq=30, sampleRate*0.45=19845], decay ∈
    // [kMinDecay=0.05, kMaxDecay=5], damping ∈ [0, 1], exciter ∈ [0, 1].
    // Out-of-range inputs must not crash, NaN, or escape the ±1.something
    // band that the bounds test (#3) verified.
    ideath::KarplusStrong ks;
    ks.prepare(kSampleRate);
    ks.setFrequency(-1000.0f);     // → 30 Hz
    ks.setDecay(-5.0f);            // → 0.05 s
    ks.setDamping(-1.0f);          // → 0
    ks.setExciter(-1.0f);          // → 0
    ks.pluck();
    for (int i = 0; i < 4410; ++i)
    {
        float v = ks.process();
        REQUIRE(std::isfinite(v));
        REQUIRE(v >= -1.0f - 1e-3f);
        REQUIRE(v <= 1.0f + 1e-3f);
    }

    ks.reset();
    ks.setFrequency(1e7f);         // → sampleRate * 0.45
    ks.setDecay(1e6f);             // → 5 s
    ks.setDamping(100.0f);         // → 1
    ks.setExciter(100.0f);         // → 1
    ks.pluck();
    for (int i = 0; i < 4410; ++i)
    {
        float v = ks.process();
        REQUIRE(std::isfinite(v));
        REQUIRE(v >= -1.0f - 1e-3f);
        REQUIRE(v <= 1.0f + 1e-3f);
    }
}

// ---------------------------------------------------------------------------
// 9. Long-run stability (≥ 10 s)
// ---------------------------------------------------------------------------

TEST_CASE("KarplusStrong: 10 seconds of continuous processing stays bounded",
          "[karplus][stability]")
{
    // Long-run stability covers two failure modes:
    //   (a) denormal accumulation in the feedback loop — DelayLine's own
    //       `+ 1e-25f` write guards the buffer; this test confirms KS does
    //       not break that guarantee with an unguarded internal state
    //       (filterState_ is also exercised here).
    //   (b) drift of the loop gain into instability — kMaxLoopGain = 0.9995
    //       keeps the loop strictly contractive, so 10 s of zero input must
    //       end ≪ peak.
    // We re-pluck every 2 s to keep the signal alive across the full run.
    ideath::KarplusStrong ks;
    ks.prepare(kSampleRate);
    ks.setFrequency(330.0f);
    ks.setDecay(1.5f);
    ks.setDamping(0.4f);
    ks.setExciter(1.0f);
    ks.pluck();

    constexpr int N = 10 * static_cast<int>(kSampleRate);  // 441000
    float maxAbs = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        if (i > 0 && (i % static_cast<int>(2.0f * kSampleRate)) == 0)
            ks.pluck();
        float v = ks.process();
        REQUIRE(std::isfinite(v));
        maxAbs = std::max(maxAbs, std::fabs(v));
    }
    // Loop gain < 1 + bounded exciter → output stays in [-1, 1] ± float
    // headroom.
    REQUIRE(maxAbs < 1.5f);
}

// ---------------------------------------------------------------------------
// 10. Parameter boundary behaviour — musically/physically meaningful extremes
// ---------------------------------------------------------------------------

TEST_CASE("KarplusStrong: damping=1 mutes the tail quickly", "[karplus][boundary]")
{
    // damping = 1 → filter pole on the unit circle. The impl guards the
    // resulting filterMagnitudeAtPitch() = 0 case by clamping loop gain to
    // kMaxLoopGain; but inside the loop, (1-d) = 0 so no new energy enters
    // the LP, and y[n] = y[n-1] — the filter just holds its last value.
    // Practically that means the loop's HF content is killed instantly and
    // only a DC term (the filter's frozen state) keeps cycling, attenuated
    // by loopGain each cycle.
    //
    // At freq=220 Hz, decay=1 s: by t = 0.2 s the peak must be substantially
    // lower than at t = 0.01 s (just after the burst). Threshold: ratio
    // peak_late / peak_early < 0.3 — a 10 dB drop is the minimum we can
    // assert without making the test brittle to the exact (1-d)/(1-d) DC
    // arithmetic, but it would still catch "damping ignored" (ratio ≈ 1).
    ideath::KarplusStrong ks;
    ks.prepare(kSampleRate);
    ks.setFrequency(220.0f);
    ks.setDecay(1.0f);
    ks.setDamping(1.0f);
    ks.setExciter(1.0f);
    ks.pluck();

    float peakEarly = peakAbs(ks, static_cast<int>(0.02f * kSampleRate));  // [0, 0.02 s]
    // Skip to 0.2 s
    for (int i = 0; i < static_cast<int>(0.18f * kSampleRate); ++i) ks.process();
    float peakLate  = peakAbs(ks, static_cast<int>(0.02f * kSampleRate));  // [0.2, 0.22 s]

    REQUIRE(peakEarly > 0.05f);
    REQUIRE(peakLate < 0.3f * peakEarly);
}

TEST_CASE("KarplusStrong: very high pitch still produces a clean tail",
          "[karplus][boundary]")
{
    // freq = 5 kHz at sr=44.1 kHz → D = 8.82 samples. The fractional delay
    // and the LP filter combined must still produce a coherent tail (no
    // NaN, no runaway). With decay=0.5 the loop gain is high (g ≈ 0.9994
    // before damping comp). The kMaxLoopGain clamp catches the edge case.
    ideath::KarplusStrong ks;
    ks.prepare(kSampleRate);
    ks.setFrequency(5000.0f);
    ks.setDecay(0.5f);
    ks.setDamping(0.2f);
    ks.setExciter(1.0f);
    ks.pluck();

    for (int i = 0; i < 22050; ++i)
    {
        float v = ks.process();
        REQUIRE(std::isfinite(v));
        REQUIRE(std::fabs(v) <= 1.5f);   // bounds, same as test #3
    }
}

TEST_CASE("KarplusStrong: minimum decay produces a fast-fading tail",
          "[karplus][boundary]")
{
    // setDecay(0.05) → clamped value kMinDecay = 0.05 s.
    // After 0.05 s the envelope should be down ~60 dB. We check that peak
    // in [0.05 s, 0.1 s] is ≪ peak in [0, 0.05 s]: ratio < 0.1 — a 20 dB
    // drop is the minimum we expect for any reasonable -60 dB tail target.
    ideath::KarplusStrong ks;
    ks.prepare(kSampleRate);
    ks.setFrequency(440.0f);
    ks.setDecay(0.05f);
    ks.setDamping(0.2f);
    ks.setExciter(1.0f);
    ks.pluck();

    float peakEarly = peakAbs(ks, static_cast<int>(0.05f * kSampleRate));
    float peakLate  = peakAbs(ks, static_cast<int>(0.05f * kSampleRate));
    REQUIRE(peakEarly > 0.05f);
    REQUIRE(peakLate < 0.1f * peakEarly);
}

TEST_CASE("KarplusStrong: exciter=0 produces no signal even with pluck()",
          "[karplus][boundary]")
{
    // setExciter(0) means the burst injects 0 * noise = 0 every sample of
    // its window. The loop content stays at zero (modulo anti-denormal),
    // so output is silent regardless of pluck() being called.
    ideath::KarplusStrong ks;
    ks.prepare(kSampleRate);
    ks.setFrequency(220.0f);
    ks.setDecay(1.0f);
    ks.setDamping(0.3f);
    ks.setExciter(0.0f);
    ks.pluck();

    for (int i = 0; i < 4410; ++i)
    {
        float v = ks.process();
        REQUIRE_THAT(v, WithinAbs(0.0f, 1e-10f));
    }
}

// ---------------------------------------------------------------------------
// 11. Extreme parameter combination — low freq × high damping × loud exciter
// ---------------------------------------------------------------------------

TEST_CASE("KarplusStrong: extreme combo (low freq + high damp + loud exciter) stable",
          "[karplus][extreme]")
{
    // The corner case: f = 30 Hz (D = 1470 samples), damping = 0.95
    // (filter heavily attenuates the loop), exciter = 1.0, decay = 5 s
    // (longest, so g_raw closest to 1).
    //
    // At 30 Hz and d=0.95:
    //   ω = 2π × 30 / 44100 ≈ 4.27e-3
    //   H(ω) = (1-0.95)/sqrt(1 - 2×0.95×cos(4.27e-3) + 0.95²)
    //        ≈ 0.05 / sqrt(0.0025 + tiny) ≈ ~0.99
    // So damping at low frequencies hardly attenuates the fundamental;
    // g ≈ g_raw ≈ 10^(-3 / (5 × 30)) = 10^-0.02 ≈ 0.955. Comfortably below
    // 0.9995, so the loop is stable on its own; this test just confirms.
    ideath::KarplusStrong ks;
    ks.prepare(kSampleRate);
    ks.setFrequency(30.0f);
    ks.setDecay(5.0f);
    ks.setDamping(0.95f);
    ks.setExciter(1.0f);
    ks.pluck();
    for (int i = 0; i < 5 * static_cast<int>(kSampleRate); ++i)
    {
        float v = ks.process();
        REQUIRE(std::isfinite(v));
        REQUIRE(std::fabs(v) <= 1.5f);
    }
}

// ---------------------------------------------------------------------------
// 11b. setFrequency() during an active pluck must preserve the ±1 bound
// ---------------------------------------------------------------------------

TEST_CASE("KarplusStrong: setFrequency mid-pluck preserves output bound",
          "[karplus][boundary]")
{
    // pluck() clamps the burst length to (delaySamples_ - 1) so the burst
    // window never overlaps itself in the delay line (see test #3 derivation).
    // But that clamp is computed against the delay length AT pluck time. If
    // setFrequency() raises the pitch (shrinks D) while the burst is still
    // in flight, the remaining burst length can exceed the new D-1, the
    // burst overlaps itself in the line, and the output overshoots ±1 by
    // up to ~2× (one feedback recirculation × loop gain + one fresh noise
    // sample at the same write position).
    //
    // This test plucks at 110 Hz (D ≈ 401 samples → burst clamped to ~44),
    // processes only 5 samples (39 of the burst still to come), then jumps
    // to 5 kHz (D ≈ 8.82) — now the remaining 39-sample burst is ≈ 4.4×
    // longer than the new delay. We then assert |output| ≤ 1.001 (the
    // canonical ±1 bound used in test #3) for the rest of the burst window
    // plus one full cycle past it.
    //
    // Threshold derivation matches test #3: nominal ±1 from the (clamped)
    // exciter + loop gain < 1, plus 1e-3 float-precision headroom from the
    // multiplicative chain.
    ideath::KarplusStrong ks;
    ks.prepare(kSampleRate);
    ks.setFrequency(110.0f);
    ks.setDecay(1.0f);
    ks.setDamping(0.0f);   // worst case: no filter loss to attenuate overlap
    ks.setExciter(1.0f);
    ks.pluck();

    // Process 5 samples at the original pitch (39 burst samples remaining).
    for (int i = 0; i < 5; ++i) (void)ks.process();

    // Mid-pluck pitch jump.
    ks.setFrequency(5000.0f);

    // Run through the rest of the burst + one cycle of the new pitch.
    // 100 samples covers ~11 cycles of 5 kHz @ 44.1 kHz, far more than the
    // ~39 remaining burst samples and any transient settling.
    for (int i = 0; i < 100; ++i)
    {
        float v = ks.process();
        REQUIRE(std::isfinite(v));
        REQUIRE(v >= -1.0f - 1e-3f);
        REQUIRE(v <= 1.0f + 1e-3f);
    }
}

// ---------------------------------------------------------------------------
// 12. Sample-rate independence — pitch matches at 44.1 kHz and 48 kHz
// ---------------------------------------------------------------------------

TEST_CASE("KarplusStrong: pitch is independent of sample rate", "[karplus][pitch]")
{
    // The KS loop's effective period in seconds = (delay + filter group delay)
    // / sampleRate. The delay is set to sampleRate/freq, so the delay-in-
    // seconds is exactly 1/freq independent of sampleRate; the one-pole LP
    // group delay (≈ d/(1-d) samples) is sub-sample at the damping used here
    // so its time contribution scales inversely with sampleRate and is
    // negligible compared to one-sample resolution.
    //
    // We measure period by locating the autocorrelation peak. The ZC-counting
    // approach used in earlier KS impls is invalid here because at any
    // moderate damping the loop carries broadband content for many cycles
    // (the one-pole LP has cutoff well above f0), so ZC count overshoots the
    // f0 prediction substantially — that test was specification-incorrect,
    // not implementation-incorrect.
    //
    // Tolerance derivation: the autocorrelation peak resolution is 1 sample.
    // At sr=48 kHz that's 1/48000 s ≈ 20.8 µs, which would be a ±4 Hz error
    // at 440 Hz — comfortably below any musically perceptible pitch error
    // (~0.1 semitone = ~25 Hz at 440), and easily catches "frequency
    // completely wrong" (a 2× pitch error would shift the lag by ≈ 50
    // samples at sr=44.1k, ≈ 500× the tolerance).
    auto measurePeriodSec = [](float sr) {
        ideath::KarplusStrong ks;
        ks.prepare(sr);
        ks.setFrequency(440.0f);
        ks.setDecay(2.0f);
        // Damping=0.4 → one-pole LP cutoff well below Nyquist, so the loop
        // settles into a near-tonal ring after a few cycles. (At damp=0.1
        // the filter is near-passthrough and the tail stays broadband.)
        ks.setDamping(0.4f);
        ks.setExciter(1.0f);
        ks.pluck();

        // Settle past the 1 ms burst and a few cycles of filter shaping.
        // 50 ms ≈ 22 cycles of 440 Hz — more than enough to reach a
        // near-stationary tonal regime under damping=0.4.
        const int settle = static_cast<int>(0.05f * sr);
        for (int i = 0; i < settle; ++i) ks.process();

        // 200 ms steady-state window. Long enough to average autocorr noise,
        // short enough that the 2-second decay envelope has only fallen by
        // ~20% (the autocorr peak still dominates).
        const int N = static_cast<int>(0.2f * sr);
        std::vector<float> buf(static_cast<std::size_t>(N));
        for (int i = 0; i < N; ++i) buf[i] = ks.process();

        // Search ±20% around the expected lag. Wide enough to detect off-by-
        // a-bit pitch errors (delay-line interp, filter group delay) but
        // narrow enough to reject the trivial corr(0) peak. A 2× pitch error
        // would lie outside this window → no false positive.
        const int expected = static_cast<int>(sr / 440.0f + 0.5f);
        const int loLag = static_cast<int>(expected * 0.8f);
        const int hiLag = static_cast<int>(expected * 1.2f);
        int bestLag = expected;
        float bestCorr = -1.0f;
        for (int lag = loLag; lag <= hiLag; ++lag)
        {
            float c = 0.0f;
            for (int i = 0; i < N - lag; ++i) c += buf[i] * buf[i + lag];
            if (c > bestCorr) { bestCorr = c; bestLag = lag; }
        }
        return static_cast<float>(bestLag) / sr;
    };

    const float period44 = measurePeriodSec(44100.0f);
    const float period48 = measurePeriodSec(48000.0f);
    const float expected = 1.0f / 440.0f;

    INFO("period44=" << period44 << " period48=" << period48 << " expected=" << expected);
    // ±1 sample of timing resolution at each sample rate.
    REQUIRE_THAT(period44, WithinAbs(expected, 1.0f / 44100.0f));
    REQUIRE_THAT(period48, WithinAbs(expected, 1.0f / 48000.0f));
}
