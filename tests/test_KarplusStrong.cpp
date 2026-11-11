#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/KarplusStrong.h>
#include <ideath/Noise.h>
#include "PitchAnalysis.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
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
    // pluck() clamps the burst length to (line delay - 1) so the burst
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

TEST_CASE("KarplusStrong: setDamping mid-pluck re-clamps the burst to the line",
          "[karplus][boundary]")
{
    // The line delay is one period minus the LP's phase delay, so raising
    // the damping shortens the line (by up to P/4). The burst must never be
    // longer than the line minus one sample — otherwise it overlaps itself
    // and breaks the ±1 bound (see test #3). At f0 = 1100 Hz, damping 0,
    // the line is ≈ 40 samples and pluck() clamps the burst to 39; damping
    // 0.95 shortens the line to ≈ 31.5 samples (τ_LP ≈ 8.5 at ω0 = 0.157).
    // After the change, the burst may run for at most int(line) − 1 more
    // samples.
    ideath::KarplusStrong ks;
    ks.prepare(kSampleRate);
    ks.setFrequency(1100.0f);
    ks.setDamping(0.0f);
    ks.setExciter(1.0f);
    ks.pluck();
    (void)ks.process();

    ks.setDamping(0.95f);
    const int maxRemaining = static_cast<int>(ks.getLineDelaySamples()) - 1;
    int remaining = 0;
    while (ks.exciterActive()) { (void)ks.process(); ++remaining; }
    INFO("line=" << ks.getLineDelaySamples() << " remaining=" << remaining);
    REQUIRE(remaining <= maxRemaining);
}

// ---------------------------------------------------------------------------
// 12. Sample-rate independence — pitch matches at 44.1 kHz and 48 kHz
// ---------------------------------------------------------------------------

TEST_CASE("KarplusStrong: pitch is independent of sample rate", "[karplus][pitch]")
{
    // The KS loop's period in seconds = (line delay + LP phase delay) /
    // sampleRate. The line delay is set to sampleRate/freq minus the LP's
    // phase delay at f0, so the loop is exactly 1/freq seconds at any
    // sample rate.
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

// ---------------------------------------------------------------------------
// 13. Tuning — the loop resonances sit on the requested pitch
// ---------------------------------------------------------------------------

TEST_CASE("KarplusStrong: partial resonances 1-3 sit within ±10 cents of the set pitch",
          "[karplus][pitch][tuning]")
{
    // The loop is delay line + one-pole LP. The LP delays the fundamental by
    // its phase delay τ(ω0) = atan(d·sin ω0 / (1 − d·cos ω0)) / ω0 samples,
    // so a delay of exactly sr/f0 makes the loop longer than one period and
    // the string rings flat (−10 c at C4 and −38 c at C6 with damping 0.5).
    // The delay must be shortened so that delay + LP phase delay + the
    // linear-interpolation phase delay is exactly one period.
    //
    // Metric: median over partials n = 1..3 of the resonance-peak position
    // (peak_n / n) relative to f0, from a 5.9 s Hann-windowed FFT
    // (0.17 Hz bins, peak refined by a log-parabola; see PitchAnalysis.h).
    //
    // Tolerance ±10 cents: the upper end of the 5-10 cent range within which
    // a mistuned sustained tone stops being heard as out of tune.
    //
    // Pitches: C2 / C4 / E4 / C6 (65.4 / 261.6 / 329.6 / 1046.5 Hz) — the
    // playing span of a plucked voice, low enough that the delay is
    // hundreds of samples and high enough that τ(ω0) is a large share of it.
    //
    // Damping 0, 0.25 and 0.5 (a typical default patch). Both setter orders
    // are exercised so the delay is recomputed whichever parameter changes
    // last.
    using namespace pitch_analysis;
    constexpr size_t kN = size_t(1) << 18;
    const double binHz = kSampleRate / static_cast<double>(kN);

    for (float f0 : { 65.4f, 261.6f, 329.6f, 1046.5f })
        for (float damping : { 0.0f, 0.25f, 0.5f })
            for (bool frequencyLast : { false, true })
            {
                ideath::KarplusStrong ks;
                ks.prepare(kSampleRate);
                if (frequencyLast) { ks.setDamping(damping); ks.setFrequency(f0); }
                else               { ks.setFrequency(f0); ks.setDamping(damping); }
                ks.setDecay(2.0f);
                ks.setExciter(1.0f);
                ks.pluck();

                std::vector<float> buf(kN);
                for (auto& s : buf) s = ks.process();

                const auto r = partialPitch(powerSpectrum(buf, kN), binHz, f0);
                INFO("f0=" << f0 << " damping=" << damping << " frequencyLast=" << frequencyLast
                     << " partials(c)=" << r.partialCents[0] << ", " << r.partialCents[1]
                     << ", " << r.partialCents[2] << " median=" << r.medianCents);
                REQUIRE(std::fabs(r.medianCents) <= 10.0);
            }
}

TEST_CASE("KarplusStrong: fundamental decays at the set rate at high pitch and damping",
          "[karplus][decay]")
{
    // The loop gain compensates the LP's and the linear interpolator's
    // magnitude at f0, so the fundamental falls 60 dB in exactly `decay`
    // seconds whatever the line fraction (up to the kMaxLoopGain ceiling).
    // Without the
    // interpolator term a fraction near ½ at C6 loses 0.9972 per cycle —
    // ≈ −50 dB extra over 2 s — so this is checked where it matters.
    //
    // Measured on the fundamental with a single-bin DFT over 0.1 s windows
    // at 0.2 s and 1.2 s after the pluck: expected drop 60 dB × 1 s / 2 s
    // = 30 dB. Tolerance ±1 dB, the just-noticeable level step; the window
    // sees the 2nd partial ≥ 10 bins away (rectangular sidelobes ≤ −30 dB,
    // and the 2nd partial is far weaker than f0 under damping), < 0.3 dB.
    auto amp = [](const std::vector<float>& b, double f) {
        double re = 0.0, im = 0.0;
        for (size_t n = 0; n < b.size(); ++n)
        {
            const double ph = 2.0 * 3.14159265358979323846 * f * double(n) / kSampleRate;
            re += b[n] * std::cos(ph);
            im += b[n] * std::sin(ph);
        }
        return std::sqrt(re * re + im * im);
    };
    struct Case { float f0, damping; };
    // Cases are chosen where the compensated gain stays under kMaxLoopGain
    // (the ceiling keeps the loop's DC gain below 1; where it binds — e.g.
    // C6 with damping 0.5, needed g ≈ 1.02 — the tail is shorter than set,
    // a documented limit). C6 / damping 0: line fraction 0.141, uncompensated
    // interpolator loss would add ≈ −12 dB/s.
    for (const Case c : { Case{ 1046.5f, 0.0f }, Case{ 329.6f, 0.3f }, Case{ 65.4f, 0.5f } })
    {
        ideath::KarplusStrong ks;
        ks.prepare(kSampleRate);
        ks.setFrequency(c.f0);
        ks.setDamping(c.damping);
        ks.setDecay(2.0f);
        ks.pluck();
        const int win = static_cast<int>(0.1f * kSampleRate);
        for (int i = 0; i < static_cast<int>(0.2f * kSampleRate); ++i) ks.process();
        std::vector<float> a(win), b(win);
        for (auto& v : a) v = ks.process();
        for (int i = 0; i < static_cast<int>(0.9f * kSampleRate); ++i) ks.process();
        for (auto& v : b) v = ks.process();
        const double drop = 20.0 * std::log10(amp(a, c.f0) / amp(b, c.f0));
        INFO("f0=" << c.f0 << " damping=" << c.damping << " drop=" << drop);
        REQUIRE(std::fabs(drop - 30.0) <= 1.0);
    }
}

// ---------------------------------------------------------------------------
// 13. No DC — the loop carries nothing below the audio band
// ---------------------------------------------------------------------------
//
// A string's velocity wave has no DC, so nothing the loop carries should sit
// below the audible band. The burst is the only thing that enters the loop;
// if its samples do not sum to zero the loop's DC mode (line + LP, DC gain g)
// keeps that offset ringing — and since g is compensated for the LP's loss
// at f0 but the LP passes DC at unity, the DC mode decays slower than the
// fundamental, so the offset outlives the note.
//
// Measurement: energy below 30 Hz, as a fraction of the total. 30 Hz is
// KarplusStrong::kMinFreq — no fundamental the string can be set to lies
// below it. The low band is taken with an 8th-order Butterworth low-pass at
// 30 Hz (double precision, four RBJ sections at the Butterworth Qs). Its
// leakage of the lowest fundamental tested, 65.4 Hz (C2), is
// |H|² = 1 / (1 + (65.4/30)^16) ≈ 4e-6 of that partial's energy — three
// orders of magnitude under the 1 % bound, so the measure sees the DC /
// sub-audio content and not the note.
//
// Bound: < 1 % of the energy. An inaudible component at 1 % of the energy
// takes 10·log10(1 / 0.99) ≈ 0.04 dB of the output's level budget (and of
// the PeakLimiter's headroom) — negligible.

namespace {

struct SubAudioMeter
{
    // RBJ low-pass sections; Qs of an 8th-order Butterworth:
    //   Q_k = 1 / (2 cos((2k − 1)π / 16)), k = 1..4.
    struct Section { double b0, b1, b2, a1, a2, z1 = 0.0, z2 = 0.0; };
    Section s[4];
    double lowEnergy = 0.0, totalEnergy = 0.0;

    explicit SubAudioMeter(double sr, double fc = 30.0)
    {
        const double pi = 3.14159265358979323846;
        const double w = 2.0 * pi * fc / sr;
        for (int k = 0; k < 4; ++k)
        {
            const double q = 1.0 / (2.0 * std::cos((2.0 * (k + 1) - 1.0) * pi / 16.0));
            const double alpha = std::sin(w) / (2.0 * q);
            const double a0 = 1.0 + alpha;
            s[k].b0 = (1.0 - std::cos(w)) / 2.0 / a0;
            s[k].b1 = (1.0 - std::cos(w)) / a0;
            s[k].b2 = s[k].b0;
            s[k].a1 = -2.0 * std::cos(w) / a0;
            s[k].a2 = (1.0 - alpha) / a0;
        }
    }
    void push(float x)
    {
        double y = x;
        for (auto& f : s)
        {
            const double out = f.b0 * y + f.z1;
            f.z1 = f.b1 * y - f.a1 * out + f.z2;
            f.z2 = f.b2 * y - f.a2 * out;
            y = out;
        }
        lowEnergy += y * y;
        totalEnergy += static_cast<double>(x) * x;
    }
    double fraction() const { return totalEnergy > 0.0 ? lowEnergy / totalEnergy : 0.0; }
};

// The four pitches tristero's PLUCK was measured at: C2, C4, E4, C6.
constexpr float kDcPitches[] = { 65.4f, 261.6f, 329.6f, 1046.5f };

} // namespace

TEST_CASE("KarplusStrong: meter separates DC from the lowest fundamental",
          "[karplus][dc]")
{
    // Self-check of the measure: a 65.4 Hz sine must read ≈ the filter's
    // leakage (≈ 4e-6, see above) — well under the 1 % bound — and a DC
    // offset of the same power must read ≈ 1 (the Butterworth's DC gain is
    // 1). Both are measured in steady state — the second of a 2 s run — so
    // the switch-on (a gated sine has real sub-audio content) and the
    // filter's own settling (≈ 0.1 s for an 8th-order Butterworth at 30 Hz)
    // are excluded. The 1e-4 / 0.99 bounds sit 25× above the 4e-6 leakage
    // and one percent under unity — margins for float-rounded input only.
    SubAudioMeter sine(kSampleRate), dc(kSampleRate);
    for (int i = 0; i < static_cast<int>(2.0f * kSampleRate); ++i)
    {
        if (i == static_cast<int>(kSampleRate))
        {
            sine.lowEnergy = sine.totalEnergy = 0.0;
            dc.lowEnergy = dc.totalEnergy = 0.0;
        }
        sine.push(static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * 65.4 * i / kSampleRate)));
        dc.push(0.7071f);
    }
    REQUIRE(sine.fraction() < 1e-4);
    REQUIRE(dc.fraction() > 0.99);
}

TEST_CASE("KarplusStrong: a pluck leaves < 1 % of its energy below 30 Hz",
          "[karplus][dc]")
{
    // Run from the pluck until every mode has decayed. The slowest is the
    // DC mode, whose per-loop gain is at most kMaxLoopGain over a loop of
    // ≈ one period P, so its −60 dB time is at most
    //   ln(1000) · P / −ln(0.9995) ≈ 13 800 · P samples,
    // 13.2 s at C6 (P = 42.1). 15 s covers that. At the lower pitches g
    // sits below the ceiling (C2: ≈ 0.90 at decay 1 s, ≈ 0.70 at 0.3 s) and
    // the period is longer, so the DC mode is down 60 dB well inside the
    // run too.
    // Decay 0.3 s and 1 s, damping 0.2 / 0.5 / 0.8: short decays make the
    // fundamental die first (the leftover DC most exposed); 0.5 is the
    // middle of the range, 0.8 the dark end.
    for (float f0 : kDcPitches)
        for (float decay : { 0.3f, 1.0f })
            for (float damping : { 0.2f, 0.5f, 0.8f })
            {
                ideath::KarplusStrong ks;
                ks.prepare(kSampleRate);
                ks.setFrequency(f0);
                ks.setDecay(decay);
                ks.setDamping(damping);
                ks.setExciter(1.0f);
                ks.pluck();
                SubAudioMeter m(kSampleRate);
                for (int i = 0; i < static_cast<int>(15.0f * kSampleRate); ++i)
                    m.push(ks.process());
                INFO("f0=" << f0 << " decay=" << decay << " damping=" << damping
                     << " fraction=" << m.fraction());
                REQUIRE(m.fraction() < 0.01);
            }
}

TEST_CASE("KarplusStrong: repeated plucks do not build up DC (12 s)",
          "[karplus][dc][stability]")
{
    // tristero re-plucks a held PLUCK note every sampleRate / 40 = 1103
    // samples. Each burst's noise is independent, so the plucks add
    // incoherently and the spectrum stays the single-pluck spectrum (no
    // 40 Hz line); any DC each burst leaves, though, adds onto the DC mode
    // that the earlier bursts left, which decays slowly (see above). Check
    // every 1 s block of a 12 s run (> 10 s long-run convention) against
    // the same < 1 % bound: a build-up shows as a block fraction that grows
    // past it. The meter runs continuously, so its start-up transient only
    // touches block 0.
    for (float f0 : kDcPitches)
    {
        ideath::KarplusStrong ks;
        ks.prepare(kSampleRate);
        ks.setFrequency(f0);
        ks.setDecay(1.0f);
        ks.setDamping(0.5f);
        ks.setExciter(1.0f);
        SubAudioMeter m(kSampleRate);
        const int block = static_cast<int>(kSampleRate);
        for (int b = 0; b < 12; ++b)
        {
            m.lowEnergy = m.totalEnergy = 0.0;
            for (int i = 0; i < block; ++i)
            {
                if ((b * block + i) % 1103 == 0)
                    ks.pluck();
                m.push(ks.process());
            }
            INFO("f0=" << f0 << " block=" << b << " fraction=" << m.fraction());
            REQUIRE(m.fraction() < 0.01);
        }
    }
}

TEST_CASE("KarplusStrong: a burst cut short still sums to zero",
          "[karplus][dc]")
{
    // Two ways a burst stops early: a re-pluck while it is in flight
    // (pluck() restarts the count), and a pitch / damping change that
    // shortens the line below the remaining burst (the count is re-clamped).
    // In both cases the samples written so far must still be balanced by
    // what comes after, or the cut-short burst leaves DC. Same < 1 % bound
    // and 15 s ring-out as the single-pluck test.
    SECTION("re-pluck mid-burst")
    {
        for (float f0 : kDcPitches)
        {
            ideath::KarplusStrong ks;
            ks.prepare(kSampleRate);
            ks.setFrequency(f0);
            ks.setDecay(1.0f);
            ks.setDamping(0.5f);
            ks.pluck();
            SubAudioMeter m(kSampleRate);
            for (int i = 0; i < 20; ++i) m.push(ks.process());
            ks.pluck();
            for (int i = 0; i < static_cast<int>(15.0f * kSampleRate); ++i)
                m.push(ks.process());
            INFO("f0=" << f0 << " fraction=" << m.fraction());
            REQUIRE(m.fraction() < 0.01);
        }
    }
    SECTION("two plucks with no sample between")
    {
        ideath::KarplusStrong ks;
        ks.prepare(kSampleRate);
        ks.setFrequency(261.6f);
        ks.setDecay(1.0f);
        ks.setDamping(0.5f);
        ks.pluck();
        ks.pluck();
        SubAudioMeter m(kSampleRate);
        for (int i = 0; i < static_cast<int>(15.0f * kSampleRate); ++i)
            m.push(ks.process());
        INFO("fraction=" << m.fraction());
        REQUIRE(m.fraction() < 0.01);
    }
    SECTION("pluck above sr/3, then the real note before any sample")
    {
        // At 19 kHz the line is ≈ 2 samples: a one-sample window that can't
        // cancel its own burst. Retuning and re-plucking before process()
        // must not inject that uncancelled sample (nothing was written).
        ideath::KarplusStrong ks;
        ks.prepare(kSampleRate);
        ks.setDecay(1.0f);
        ks.setDamping(0.5f);
        ks.setFrequency(19000.0f);
        ks.pluck();
        ks.setFrequency(261.6f);
        ks.pluck();
        SubAudioMeter m(kSampleRate);
        for (int i = 0; i < static_cast<int>(15.0f * kSampleRate); ++i)
            m.push(ks.process());
        INFO("fraction=" << m.fraction());
        REQUIRE(m.fraction() < 0.01);
    }
    SECTION("level changed, then re-pluck mid-window")
    {
        // tristero sets the exciter level from the velocity right before
        // each pluck(). At C2 the correction window (672 samples) is still
        // running 300 samples after the first pluck, so the re-pluck must
        // cancel what was written at the old level.
        ideath::KarplusStrong ks;
        ks.prepare(kSampleRate);
        ks.setFrequency(65.4f);
        ks.setDecay(1.0f);
        ks.setDamping(0.5f);
        ks.setExciter(1.0f);
        ks.pluck();
        SubAudioMeter m(kSampleRate);
        for (int i = 0; i < 300; ++i) m.push(ks.process());
        ks.setExciter(0.3f);
        ks.pluck();
        for (int i = 0; i < static_cast<int>(15.0f * kSampleRate); ++i)
            m.push(ks.process());
        INFO("fraction=" << m.fraction());
        REQUIRE(m.fraction() < 0.01);
    }
    SECTION("pitch raised mid-burst")
    {
        // 110 Hz burst = 44 samples; after 5, jump to 2093 Hz (line ≈ 20
        // at damping 0.5), so the window is cut to (line − 1) − 5 samples
        // and the remaining burst with it.
        ideath::KarplusStrong ks;
        ks.prepare(kSampleRate);
        ks.setFrequency(110.0f);
        ks.setDecay(1.0f);
        ks.setDamping(0.5f);
        ks.pluck();
        SubAudioMeter m(kSampleRate);
        for (int i = 0; i < 5; ++i) m.push(ks.process());
        ks.setFrequency(2093.0f);
        for (int i = 0; i < static_cast<int>(15.0f * kSampleRate); ++i)
            m.push(ks.process());
        INFO("fraction=" << m.fraction());
        REQUIRE(m.fraction() < 0.01);
    }
}

TEST_CASE("KarplusStrong: pitch set right after pluck() excites the new note fully",
          "[karplus][dc]")
{
    // A host may pluck first and push the new note's pitch on the next
    // sample (per-sample setters). The DC correction window must then span
    // one loop length of the new pitch from the pluck — on a drop A3 → A2,
    // a window sized to A3 covers half an A2 period, and a constant over
    // half a period has a component at f0 that cancels part of the
    // fundamental. Spec: plucking at A3 and retuning to A2 before the first
    // sample is the same exciter as plucking at A2: same noise (fresh
    // instances, deterministic seed), same burst count (44 ≤ both lines),
    // same window, so the outputs agree sample for sample. Tolerance 1e-6:
    // float rounding of the plan (double → float) at amplitude ≤ 1 is
    // ≤ 6e-8 per sample; the loop is contractive, so rounding does not grow
    // past a few ulps of the peak.
    auto make = [](float first) {
        ideath::KarplusStrong ks;
        ks.prepare(kSampleRate);
        ks.setDecay(1.0f);
        ks.setDamping(0.5f);
        ks.setFrequency(first);
        return ks;
    };
    ideath::KarplusStrong direct = make(110.0f);
    ideath::KarplusStrong retuned = make(220.0f);
    direct.pluck();
    retuned.pluck();
    retuned.setFrequency(110.0f);
    float maxDiff = 0.0f;
    for (int i = 0; i < static_cast<int>(0.5f * kSampleRate); ++i)
        maxDiff = std::max(maxDiff, std::fabs(direct.process() - retuned.process()));
    INFO("maxDiff=" << maxDiff);
    REQUIRE(maxDiff < 1e-6f);
}

TEST_CASE("KarplusStrong: the exciter sums to zero and keeps the burst's fundamental",
          "[karplus][dc]")
{
    // The spec behind the design, checked on the exciter itself. Until the
    // first sample comes back round (≈ 2 line lengths after the pluck), the
    // output is just the exciter read through the line, so over that first
    // pass:
    //  (a) its sum is zero — nothing feeds the loop's DC mode. Bound 1e-4:
    //      float rounding of ≈ 700 terms ≤ 1 at ≈ 6e-8 each is ≈ 4e-5.
    //  (b) its component at f0 matches the raw noise burst's (same seed:
    //      KarplusStrong's Noise is default-seeded and reset in prepare()),
    //      within 0.5 dB. A zero-mean burst would lose ≈ 22 dB here at C2
    //      (the 1 ms burst's spectrum is flat from DC past f0, so its mean is
    //      its fundamental). The correction is a constant over M = line − 1
    //      samples, P − M ≈ 2.6 short of a period, so its f0 component is
    //      |S|/M · |sin(ω0 M / 2) / sin(ω0 / 2)| ≈ |S| · 2.6 / M ≈ 0.4 % of
    //      |S| ≈ |X(f0)|; the ±1 scaling costs ≤ |S|/M ≈ 1 % (0.09 dB);
    //      the interpolator's magnitude at f0 is ≥ 0.9999. Total < 0.15 dB;
    //      0.5 dB leaves margin under the 1 dB level JND.
    const double pi = 3.14159265358979323846;
    const double f0 = 65.4;
    ideath::KarplusStrong ks;
    ks.prepare(kSampleRate);
    ks.setFrequency(static_cast<float>(f0));
    ks.setDecay(1.0f);
    ks.setDamping(0.5f);
    ks.setExciter(1.0f);
    ks.pluck();

    // Exciter sample n (n ≤ int(line) − 2) is read at t = n + int(line)
    // and n + int(line) + 1 (linear interpolation); the first sample fed
    // back is written at t = int(line) and read again at 2·int(line). So
    // t < 2·int(line) is the whole first pass and nothing else.
    const int firstPass = 2 * static_cast<int>(ks.getLineDelaySamples());
    const double w = 2.0 * pi * f0 / kSampleRate;
    double sum = 0.0, re = 0.0, im = 0.0;
    for (int t = 0; t < firstPass; ++t)
    {
        const double y = ks.process();
        sum += y;
        re += y * std::cos(w * t);
        im -= y * std::sin(w * t);
    }
    ideath::Noise raw;
    double rre = 0.0, rim = 0.0;
    const int burst = static_cast<int>(ideath::KarplusStrong::kExciterSec * kSampleRate);
    for (int n = 0; n < burst; ++n)
    {
        const double r = raw.process();
        rre += r * std::cos(w * n);
        rim -= r * std::sin(w * n);
    }
    const double levelDb = 20.0 * std::log10(std::hypot(re, im) / std::hypot(rre, rim));
    INFO("sum=" << sum << " f0 level vs raw burst=" << levelDb << " dB");
    REQUIRE(std::fabs(sum) < 1e-4);
    REQUIRE(std::fabs(levelDb) < 0.5);
}

// ---------------------------------------------------------------------------
// Exciter tone (setExciterTone) — the pluck's own brightness
// ---------------------------------------------------------------------------

namespace {

// FNV-1a 64 over the bit patterns of a run of output samples. Exact
// equality of bits, not closeness: tone 0.5 must leave the output that
// tristero's saved sounds were rendered with untouched.
struct BitHash
{
    std::uint64_t h = 1469598103934665603ull;
    void push(float v)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        for (int b = 0; b < 4; ++b)
        {
            h ^= (bits >> (8 * b)) & 0xffu;
            h *= 1099511628211ull;
        }
    }
};

// Every exciter path the plan has: single plucks across pitch × damping
// (incl. the one-sample window above sr/3), a re-pluck mid-burst, a level
// change and re-pluck mid-window, pitch raised mid-burst (window cut), pitch
// dropped mid-window (window grown), damping changed mid-burst, and 40 Hz
// re-plucks. `configure` runs on each fresh instance before anything else.
template <typename Configure>
std::uint64_t exciterScenarioHash(Configure configure)
{
    BitHash hash;
    auto fresh = [&](float f0, float damping) {
        ideath::KarplusStrong ks;
        ks.prepare(kSampleRate);
        configure(ks);
        ks.setFrequency(f0);
        ks.setDecay(1.0f);
        ks.setDamping(damping);
        ks.setExciter(1.0f);
        return ks;
    };
    auto run = [&](ideath::KarplusStrong& ks, int n) {
        for (int i = 0; i < n; ++i) hash.push(ks.process());
    };
    for (float f0 : { 30.0f, 65.4f, 261.6f, 1046.5f, 5000.0f, 19000.0f })
        for (float damping : { 0.0f, 0.5f, 1.0f })
        {
            ideath::KarplusStrong ks = fresh(f0, damping);
            ks.pluck();
            run(ks, 4410);
        }
    {
        ideath::KarplusStrong ks = fresh(110.0f, 0.5f);
        ks.pluck(); run(ks, 20); ks.pluck(); run(ks, 4410);
    }
    {
        ideath::KarplusStrong ks = fresh(65.4f, 0.5f);
        ks.pluck(); run(ks, 300); ks.setExciter(0.3f); ks.pluck(); run(ks, 4410);
    }
    {
        ideath::KarplusStrong ks = fresh(110.0f, 0.5f);
        ks.pluck(); run(ks, 5); ks.setFrequency(2093.0f); run(ks, 4410);
    }
    {
        ideath::KarplusStrong ks = fresh(220.0f, 0.5f);
        ks.pluck(); run(ks, 60); ks.setFrequency(65.4f); run(ks, 4410);
    }
    {
        ideath::KarplusStrong ks = fresh(261.6f, 0.2f);
        ks.pluck(); run(ks, 10); ks.setDamping(0.9f); run(ks, 4410);
    }
    {
        ideath::KarplusStrong ks = fresh(329.6f, 0.5f);
        for (int i = 0; i < 44100; ++i)
        {
            if (i % 1103 == 0) ks.pluck();
            hash.push(ks.process());
        }
    }
    return hash.h;
}

} // namespace

TEST_CASE("KarplusStrong: default exciter output is unchanged (bit-exact reference)",
          "[karplus][tone]")
{
    // Reference: the hash of exciterScenarioHash() recorded from the
    // raw-burst exciter, before any tone filter was added (same build).
    // Spec: default tone (0.5) reproduces it bit for bit, so saved
    // tristero PLUCK sounds do not move.
    const std::uint64_t h = exciterScenarioHash([](ideath::KarplusStrong&) {});
    INFO("hash=0x" << std::hex << h);
    REQUIRE(h == 0xd4557860c8424e4eull);
}

TEST_CASE("KarplusStrong: exciter tone 0.5 is the default exciter, bit for bit",
          "[karplus][tone]")
{
    // Same scenario as the reference above, with the tone set explicitly —
    // directly, and after a trip through both ends (the setter keeps no
    // other state). Exact hash equality, for the same reason.
    const std::uint64_t ref = exciterScenarioHash([](ideath::KarplusStrong&) {});
    REQUIRE(exciterScenarioHash([](ideath::KarplusStrong& ks) {
                ks.setExciterTone(0.5f);
            }) == ref);
    REQUIRE(exciterScenarioHash([](ideath::KarplusStrong& ks) {
                ks.setExciterTone(0.0f);
                ks.setExciterTone(1.0f);
                ks.setExciterTone(0.5f);
            }) == ref);
    // Other tones do change the output (guards against a setter that is
    // ignored, which would also pass the two checks above).
    REQUIRE(exciterScenarioHash([](ideath::KarplusStrong& ks) {
                ks.setExciterTone(0.0f);
            }) != ref);
    REQUIRE(exciterScenarioHash([](ideath::KarplusStrong& ks) {
                ks.setExciterTone(1.0f);
            }) != ref);
}

TEST_CASE("KarplusStrong: exciter tone clamps to [0, 1]", "[karplus][tone][boundary]")
{
    // Out-of-range tones behave exactly like the nearest end (exact hash
    // equality: the clamp happens before anything is derived from the tone).
    auto at = [](float tone) {
        return exciterScenarioHash([tone](ideath::KarplusStrong& ks) {
            ks.setExciterTone(tone);
        });
    };
    REQUIRE(at(-3.0f) == at(0.0f));
    REQUIRE(at(7.0f) == at(1.0f));
}

TEST_CASE("KarplusStrong: exciter tone is taken at pluck()", "[karplus][tone]")
{
    // Like the level: an exciter in flight keeps the tone it was plucked
    // with. Changing the tone 5 samples into a pluck (burst and LP tail
    // both still running, window 398 samples at A2) leaves the output
    // identical, bit for bit, to not changing it.
    for (float plucked : { 0.0f, 1.0f })
    {
        auto make = [plucked]() {
            ideath::KarplusStrong ks;
            ks.prepare(kSampleRate);
            ks.setFrequency(110.0f);
            ks.setDecay(1.0f);
            ks.setDamping(0.5f);
            ks.setExciterTone(plucked);
            ks.pluck();
            return ks;
        };
        ideath::KarplusStrong steady = make();
        ideath::KarplusStrong changed = make();
        bool same = true;
        for (int i = 0; i < 22050; ++i)
        {
            if (i == 5)
                changed.setExciterTone(1.0f - plucked);
            const float a = steady.process(), b = changed.process();
            same = same && std::memcmp(&a, &b, sizeof a) == 0;
        }
        INFO("plucked tone=" << plucked);
        REQUIRE(same);
    }
}

namespace {

// The tone filters as specified (KarplusStrong.h), evaluated in double:
//   tone < 0.5: one-pole LP, pole a = exp(−1 / (τ·sr)),
//               τ = kSoftReleaseSec · (1 − 2·tone).
//   tone > 0.5: high shelf x + g·(x − LP_h(x)), g = (2·tone − 1) ·
//               kHardShelfGain, LP_h pole a = exp(−2π·kHardShelfHz / sr).
// Returns |H(e^jω)|².
double toneFilterPower(float tone, double w, double sr)
{
    const double pi = 3.14159265358979323846;
    if (tone == 0.5f)
        return 1.0;
    // One-pole LP (1 − a) / (1 − a·e^−jω), as a complex number.
    auto lp = [w](double a, double& re, double& im) {
        const double dr = 1.0 - a * std::cos(w), di = a * std::sin(w);
        const double den = dr * dr + di * di;
        re = (1.0 - a) * dr / den;
        im = -(1.0 - a) * di / den;
    };
    double re, im;
    if (tone < 0.5f)
    {
        const double tau = ideath::KarplusStrong::kSoftReleaseSec * (1.0 - 2.0 * tone);
        lp(std::exp(-1.0 / (tau * sr)), re, im);
        return re * re + im * im;
    }
    const double g = (2.0 * tone - 1.0) * ideath::KarplusStrong::kHardShelfGain;
    lp(std::exp(-2.0 * pi * ideath::KarplusStrong::kHardShelfHz / sr), re, im);
    const double hr = 1.0 + g * (1.0 - re), hi = -g * im;
    return hr * hr + hi * hi;
}

} // namespace

TEST_CASE("KarplusStrong: exciter tone sets the pluck's high-band balance",
          "[karplus][tone]")
{
    // Measure: energy at harmonics 9 and up over energy at harmonics 1-8
    // ("above the 8th partial" vs the low partials), of the exciter alone —
    // the first pass, t < 2·int(line), before anything comes back round
    // (see "the exciter sums to zero" above). The whole exciter lies inside
    // that pass, so the DTFT at k·f0 of the pass is the exciter's exactly.
    // Harmonics only: the DC correction (a constant over M samples, M one
    // loop length less ≈ 2.9 at A2) is ≈ 0 there (≈ 2.9 / M = 0.73 % of
    // the burst's sum, < 0.07 dB), while between harmonics it is not.
    //
    // Prediction: for a white burst x (any length), E|X(ω)|² is the same at
    // every ω, so E|Y(ω)|² ∝ |H_tone(ω)|² · |I(ω)|², I the line's linear
    // interpolator (1 − f) + f·e^−jω. The band ratio's expectation is the
    // ratio of those sums over the same harmonics.
    //
    // Tolerance ±1.5 dB, from the spread of the estimate: a 1 ms burst
    // resolves ≈ 1 kHz, so harmonics 1-8 at A2 (110-880 Hz) carry ≈ one
    // complex Gaussian value per pluck (2 degrees of freedom); 128 plucks →
    // 256 dof → relative std of the low-band energy √(2/256) = 0.088,
    // 0.38 dB; ±1.5 dB is 4 σ. Level 0.3 keeps every burst unscaled (the
    // ±1 scaling would otherwise weight plucks by their peak):
    // |y| ≤ (1 + g)|x| + g|LP| ≤ 1 + 2g = 3 at tone 1 (g = kHardShelfGain),
    // so 0.3·3 + 0.3·|S|/M ≤ 0.9 + 0.3·44/398 = 0.93 < 1.
    //
    // "Clearly changes": tone 1 must sit ≥ 10 dB above tone 0 — 10 dB is
    // the step heard as twice (or half) as loud (Stevens' sone scale), so
    // the high band's share of the pluck doubles at least.
    //
    // Plucks 0.5 s apart at decay kMinDecay = 0.05 s: the previous note is
    // down 10 × 60 dB before the next pass starts.
    const double pi = 3.14159265358979323846;
    const double f0 = 110.0;
    const double w0 = 2.0 * pi * f0 / kSampleRate;
    const int kMaxHarm = static_cast<int>(0.5 * kSampleRate / f0);

    auto measure = [&](float tone, double& predictedDb) {
        ideath::KarplusStrong ks;
        ks.prepare(kSampleRate);
        ks.setFrequency(static_cast<float>(f0));
        ks.setDecay(ideath::KarplusStrong::kMinDecay);
        ks.setDamping(0.5f);
        ks.setExciter(0.3f);
        ks.setExciterTone(tone);
        const int pass = 2 * static_cast<int>(ks.getLineDelaySamples());
        const double frac = ks.getLineDelaySamples()
                          - std::floor(ks.getLineDelaySamples());
        double lowP = 0.0, highP = 0.0;
        for (int k = 1; k <= kMaxHarm; ++k)
        {
            const double w = k * w0;
            const double ir = (1.0 - frac) + frac * std::cos(w);
            const double ii = -frac * std::sin(w);
            const double p = toneFilterPower(tone, w, kSampleRate) * (ir * ir + ii * ii);
            (k <= 8 ? lowP : highP) += p;
        }
        predictedDb = 10.0 * std::log10(highP / lowP);

        std::vector<float> y(pass);
        double low = 0.0, high = 0.0;
        for (int p = 0; p < 128; ++p)
        {
            ks.pluck();
            for (int t = 0; t < pass; ++t) y[t] = ks.process();
            for (int k = 1; k <= kMaxHarm; ++k)
            {
                double re = 0.0, im = 0.0;
                const double w = k * w0;
                for (int t = 0; t < pass; ++t)
                {
                    re += y[t] * std::cos(w * t);
                    im -= y[t] * std::sin(w * t);
                }
                (k <= 8 ? low : high) += re * re + im * im;
            }
            for (int t = pass; t < static_cast<int>(0.5f * kSampleRate); ++t)
                ks.process();
        }
        return 10.0 * std::log10(high / low);
    };

    double pred0, pred5, pred1;
    const double db0 = measure(0.0f, pred0);
    const double db5 = measure(0.5f, pred5);
    const double db1 = measure(1.0f, pred1);
    INFO("DIAG high/low (dB) tone 0: " << db0 << " (pred " << pred0 << ")"
         << "  0.5: " << db5 << " (pred " << pred5 << ")"
         << "  1: " << db1 << " (pred " << pred1 << ")");
    CHECK(std::fabs(db0 - pred0) < 1.5);
    CHECK(std::fabs(db5 - pred5) < 1.5);
    CHECK(std::fabs(db1 - pred1) < 1.5);
    REQUIRE(db1 - db0 >= 10.0);
}

namespace {

// −60 dB time of the fundamental: amplitude at f0 over consecutive
// one-period blocks (DTFT over round(P) samples; the 2nd and higher
// harmonics sit on that window's zeros up to the < 0.5-sample rounding of
// P), from the block starting at `from` seconds to the first block that is
// 60 dB below it. Returns seconds.
double fundamentalT60(ideath::KarplusStrong& ks, double f0, double from)
{
    const double pi = 3.14159265358979323846;
    const int P = static_cast<int>(std::lround(kSampleRate / f0));
    const double w = 2.0 * pi * f0 / kSampleRate;
    const int skip = static_cast<int>(from * kSampleRate);
    for (int i = 0; i < skip; ++i) ks.process();
    double ref = -1.0;
    for (int b = 0; b < static_cast<int>(10.0 * kSampleRate) / P; ++b)
    {
        double re = 0.0, im = 0.0;
        for (int t = 0; t < P; ++t)
        {
            const double y = ks.process();
            re += y * std::cos(w * t);
            im -= y * std::sin(w * t);
        }
        const double a = std::hypot(re, im);
        if (ref < 0.0)
            ref = a;
        else if (a <= ref * 1e-3)
            return static_cast<double>(b) * P / kSampleRate;
    }
    return -1.0;
}

} // namespace

TEST_CASE("KarplusStrong: exciter tone leaves the decay length unchanged",
          "[karplus][tone][decay]")
{
    // The tone only shapes what the exciter writes; loop gain and line
    // length are untouched, so the fundamental's −60 dB time is the set
    // decay for every tone. Spec (setExciterTone): the tone leaves the
    // decay length unchanged, within ±10 % of setDecay. The fundamental, not the broadband envelope: a
    // brighter pluck puts more of its energy in partials that the loop LP
    // damps faster, so a broadband −60 dB re its own peak is reached
    // sooner without the string decaying any faster. Measured from 0.1 s
    // (exciter and first round trips long over at 220 Hz).
    for (float tone : { 0.0f, 0.5f, 1.0f })
        for (float damping : { 0.3f, 0.7f })
        {
            ideath::KarplusStrong ks;
            ks.prepare(kSampleRate);
            ks.setFrequency(220.0f);
            ks.setDecay(1.0f);
            ks.setDamping(damping);
            ks.setExciterTone(tone);
            ks.pluck();
            const double t60 = fundamentalT60(ks, 220.0, 0.1);
            INFO("DIAG tone=" << tone << " damping=" << damping << " T60(f0)=" << t60);
            REQUIRE(t60 > 0.9);
            REQUIRE(t60 < 1.1);
        }
}

TEST_CASE("KarplusStrong: exciter tone keeps the output within ±1",
          "[karplus][tone][bounds]")
{
    // The ±1 contract is per exciter: what one pluck writes stays within ±1
    // and never overlaps itself in the line, so a single pluck's output is
    // bounded (see "output stays within ±1"; same 1e-3 float headroom).
    // The ±1 scaling is computed on the filtered exciter and the filter's
    // tail is cut at the window's end, so the same holds for every tone:
    // single plucks across pitch × damping (incl. the one-sample window at
    // 19 kHz) at the longest decay, and a pluck whose window is cut (pitch
    // raised mid-burst) or grown (pitch dropped while the LP tail runs).
    // Re-plucks on a sounding string add to what is already in the loop
    // and are not covered by this contract, with or without a tone.
    auto check = [](ideath::KarplusStrong& ks, int n) {
        for (int i = 0; i < n; ++i)
        {
            const float v = ks.process();
            REQUIRE(std::isfinite(v));
            REQUIRE(std::fabs(v) <= 1.0f + 1e-3f);
        }
    };
    for (float tone : { 0.0f, 0.25f, 0.75f, 1.0f })
    {
        INFO("tone=" << tone);
        for (float freq : { 30.0f, 110.0f, 440.0f, 2000.0f, 5000.0f, 19000.0f })
            for (float damp : { 0.0f, 0.3f, 0.7f, 1.0f })
            {
                INFO("freq=" << freq << " damp=" << damp);
                ideath::KarplusStrong ks;
                ks.prepare(kSampleRate);
                ks.setFrequency(freq);
                ks.setDecay(5.0f);    // longest ring: loop content stays largest
                ks.setDamping(damp);
                ks.setExciter(1.0f);
                ks.setExciterTone(tone);
                ks.pluck();
                check(ks, 22050);
            }
        ideath::KarplusStrong ks;
        ks.prepare(kSampleRate);
        ks.setDecay(5.0f);
        ks.setDamping(0.5f);
        ks.setExciterTone(tone);
        ks.setFrequency(110.0f);
        ks.pluck();
        check(ks, 5);
        ks.setFrequency(2093.0f);
        check(ks, 22050);
        ks.reset();
        ks.setFrequency(220.0f);
        ks.pluck();
        check(ks, 30);
        ks.setFrequency(30.0f);
        check(ks, 22050);
    }
}

TEST_CASE("KarplusStrong: exciter tone stays within ±1 in the one-sample window",
          "[karplus][tone][bounds]")
{
    // Above ≈ 14 kHz the line is shorter than 3 samples, so the correction
    // window M = int(line) − 1 is one sample (M < 2): the burst is a single sample with no correction.
    // Through the shelf that sample is x·(1 + g·a) (LP state starts at 0),
    // up to 1.75·|x| at tone 1, so the ±1 scaling must apply here too:
    // the same per-pluck bound (1e-3 float headroom). Many noise draws per
    // pitch (30 plucks, 0.5 s apart at decay kMinDecay = 0.05 s, so each
    // starts on a string 600 dB down), since one draw near 0 hides it.
    for (float freq : { 14000.0f, 15000.0f, 16000.0f, 19000.0f })
        for (float damp : { 0.0f, 0.7f })
            for (float tone : { 0.75f, 1.0f })
            {
                INFO("freq=" << freq << " damp=" << damp << " tone=" << tone);
                ideath::KarplusStrong ks;
                ks.prepare(kSampleRate);
                ks.setFrequency(freq);
                ks.setDecay(ideath::KarplusStrong::kMinDecay);
                ks.setDamping(damp);
                ks.setExciter(1.0f);
                ks.setExciterTone(tone);
                float peak = 0.0f;
                for (int p = 0; p < 30; ++p)
                {
                    ks.pluck();
                    peak = std::max(peak, peakAbs(ks, static_cast<int>(0.5f * kSampleRate)));
                }
                INFO("peak=" << peak);
                REQUIRE(peak <= 1.0f + 1e-3f);
            }
}

TEST_CASE("KarplusStrong: exciter tone leaves no DC", "[karplus][tone][dc]")
{
    // The sum cancellation runs on the filtered exciter, so the < 1 %
    // sub-30 Hz bound (and 15 s ring-out) of the DC tests above holds for
    // every tone; the LP tail is the new part (it outlasts the burst and
    // must be counted in the sum, and in what a re-plan sees as written).
    auto ringOut = [](ideath::KarplusStrong& ks, SubAudioMeter& m) {
        for (int i = 0; i < static_cast<int>(15.0f * kSampleRate); ++i)
            m.push(ks.process());
    };
    auto make = [](float f0, float tone) {
        ideath::KarplusStrong ks;
        ks.prepare(kSampleRate);
        ks.setFrequency(f0);
        ks.setDecay(1.0f);
        ks.setDamping(0.5f);
        ks.setExciter(1.0f);
        ks.setExciterTone(tone);
        return ks;
    };
    SECTION("single pluck")
    {
        for (float tone : { 0.0f, 1.0f })
            for (float f0 : kDcPitches)
            {
                ideath::KarplusStrong ks = make(f0, tone);
                ks.pluck();
                SubAudioMeter m(kSampleRate);
                ringOut(ks, m);
                INFO("tone=" << tone << " f0=" << f0 << " fraction=" << m.fraction());
                REQUIRE(m.fraction() < 0.01);
            }
    }
    SECTION("re-pluck with another tone mid-burst and mid-tail")
    {
        for (int at : { 20, 150 })
            for (float f0 : kDcPitches)
            {
                ideath::KarplusStrong ks = make(f0, 0.0f);
                ks.pluck();
                SubAudioMeter m(kSampleRate);
                for (int i = 0; i < at; ++i) m.push(ks.process());
                ks.setExciterTone(1.0f);
                ks.pluck();
                ringOut(ks, m);
                INFO("at=" << at << " f0=" << f0 << " fraction=" << m.fraction());
                REQUIRE(m.fraction() < 0.01);
            }
    }
    SECTION("pitch dropped while the LP tail runs, raised mid-burst")
    {
        {
            ideath::KarplusStrong ks = make(261.6f, 0.0f);
            ks.pluck();
            SubAudioMeter m(kSampleRate);
            for (int i = 0; i < 60; ++i) m.push(ks.process());
            ks.setFrequency(65.4f);
            ringOut(ks, m);
            INFO("drop fraction=" << m.fraction());
            REQUIRE(m.fraction() < 0.01);
        }
        for (float tone : { 0.0f, 1.0f })
        {
            ideath::KarplusStrong ks = make(110.0f, tone);
            ks.pluck();
            SubAudioMeter m(kSampleRate);
            for (int i = 0; i < 5; ++i) m.push(ks.process());
            ks.setFrequency(2093.0f);
            ringOut(ks, m);
            INFO("raise tone=" << tone << " fraction=" << m.fraction());
            REQUIRE(m.fraction() < 0.01);
        }
    }
    SECTION("40 Hz re-plucks with alternating tone (12 s)")
    {
        // As "repeated plucks do not build up DC", each pluck at the other
        // end of the tone range.
        for (float f0 : kDcPitches)
        {
            ideath::KarplusStrong ks = make(f0, 0.0f);
            SubAudioMeter m(kSampleRate);
            const int block = static_cast<int>(kSampleRate);
            int n = 0;
            for (int b = 0; b < 12; ++b)
            {
                m.lowEnergy = m.totalEnergy = 0.0;
                for (int i = 0; i < block; ++i)
                {
                    if ((b * block + i) % 1103 == 0)
                    {
                        ks.setExciterTone((n++ % 2) ? 1.0f : 0.0f);
                        ks.pluck();
                    }
                    m.push(ks.process());
                }
                INFO("f0=" << f0 << " block=" << b << " fraction=" << m.fraction());
                REQUIRE(m.fraction() < 0.01);
            }
        }
    }
}

TEST_CASE("KarplusStrong: a toned exciter sums to zero and keeps H(f0) of the fundamental",
          "[karplus][tone][dc]")
{
    // As "the exciter sums to zero and keeps the burst's fundamental",
    // through the tone filter: over the first pass (a) the sum is zero
    // (same 1e-4 rounding bound), (b) the f0 component is the raw burst's
    // times the filter's response at f0: both filters have DC gain 1 and
    // the burst's low partials ride on its sum, so a soft pluck keeps its
    // fundamental (tone 0 at C2: |H| = −0.18 dB). Same 0.5 dB bound
    // (≤ 0.15 dB from the correction window and the interpolator, see that
    // test). Level 0.3 keeps the burst unscaled (see the band test), so the
    // reference is 0.3 × the raw burst.
    const double pi = 3.14159265358979323846;
    const double f0 = 65.4;
    const double w = 2.0 * pi * f0 / kSampleRate;
    for (float tone : { 0.0f, 0.25f, 0.75f, 1.0f })
    {
        ideath::KarplusStrong ks;
        ks.prepare(kSampleRate);
        ks.setFrequency(static_cast<float>(f0));
        ks.setDecay(1.0f);
        ks.setDamping(0.5f);
        ks.setExciter(0.3f);
        ks.setExciterTone(tone);
        ks.pluck();
        const int firstPass = 2 * static_cast<int>(ks.getLineDelaySamples());
        double sum = 0.0, re = 0.0, im = 0.0;
        for (int t = 0; t < firstPass; ++t)
        {
            const double y = ks.process();
            sum += y;
            re += y * std::cos(w * t);
            im -= y * std::sin(w * t);
        }
        ideath::Noise raw;
        double rre = 0.0, rim = 0.0;
        const int burst = static_cast<int>(ideath::KarplusStrong::kExciterSec * kSampleRate);
        for (int n = 0; n < burst; ++n)
        {
            const double r = 0.3 * raw.process();
            rre += r * std::cos(w * n);
            rim -= r * std::sin(w * n);
        }
        const double levelDb = 20.0 * std::log10(std::hypot(re, im) / std::hypot(rre, rim));
        const double expectDb = 10.0 * std::log10(toneFilterPower(tone, w, kSampleRate));
        INFO("tone=" << tone << " sum=" << sum << " f0 level " << levelDb
             << " dB, expected " << expectDb << " dB");
        REQUIRE(std::fabs(sum) < 1e-4);
        REQUIRE(std::fabs(levelDb - expectDb) < 0.5);
    }
}

TEST_CASE("KarplusStrong: exciter tone has no step at 0.5", "[karplus][tone][boundary]")
{
    // The knob's centre is the raw burst; just either side must be almost
    // the same pluck. Over the first pass (exciter only), at level 0.3 (no
    // ±1 scaling, see the band test):
    //   tone 0.5 + δ: y − x = g·(x − LP(x)), g = 2δ·kHardShelfGain,
    //     |x − LP(x)| ≤ 2 → |Δexciter| ≤ 0.3·2g; the correction moves by
    //     the change of the sum over M, ≤ 0.3·2g·B/M (B = 44 burst samples,
    //     M = 398 at A2); the interpolator is a convex mix. So
    //     |Δy| ≤ 0.6·g·(1 + 44/398) = 0.67·g = 0.0013 at δ = 0.001.
    //   tone 0.5 − δ: pole a = exp(−1/(2δ·kSoftReleaseSec·sr)) = 1.4e-10 at
    //     δ = 0.001, so |Δ| is of that order — far inside the same bound.
    // Bound 0.002 (≈ −54 dBFS): above 0.0013, and a step that small at the
    // centre detent is inaudible next to the burst itself.
    auto firstPass = [](float tone) {
        ideath::KarplusStrong ks;
        ks.prepare(kSampleRate);
        ks.setFrequency(110.0f);
        ks.setDecay(1.0f);
        ks.setDamping(0.5f);
        ks.setExciter(0.3f);
        ks.setExciterTone(tone);
        ks.pluck();
        std::vector<float> y(2 * static_cast<int>(ks.getLineDelaySamples()));
        for (float& v : y) v = ks.process();
        return y;
    };
    const std::vector<float> mid = firstPass(0.5f);
    for (float tone : { 0.499f, 0.501f })
    {
        const std::vector<float> near = firstPass(tone);
        float maxDiff = 0.0f;
        for (size_t i = 0; i < mid.size(); ++i)
            maxDiff = std::max(maxDiff, std::fabs(near[i] - mid[i]));
        INFO("tone=" << tone << " maxDiff=" << maxDiff);
        REQUIRE(maxDiff < 0.002f);
    }
}
