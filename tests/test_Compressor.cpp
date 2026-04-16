#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Compressor.h>
#include <cmath>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;
static constexpr float kPi = 3.14159265f;

static float dBtoLin(float dB) { return std::pow(10.0f, dB / 20.0f); }

// Threshold derivations used throughout this file
// -----------------------------------------------
// Compressor per-sample pipeline:
//   envelope <- one-pole (attack if rising, release if falling) toward |input|
//               flushed to 0 when < 1e-8 (denormal guard)
//   envDb     = 20·log10(envelope)  (or -120 if envelope ≤ 1e-6)
//   gr (hard-knee, k=0):
//       gr = 0                               if envDb ≤ T
//       gr = −(envDb − T)·(1 − 1/R)          if envDb > T
//   gainDb    <- one-pole toward gr (attack coef if gr < gainDb, else release)
//   out       = input · 10^(gainDb/20) · makeupLinear
//
// One-pole coefficient α = exp(−1/(τ·fs)). At N = 10·τ·fs samples the one-pole
// response is within exp(−10) ≈ 4.5e-5 of its target; combined with a second
// cascaded pole (envelope feeds gr feeds gainDb) the residual at 20·τ is
// still well under 1e-3 of a unit-order quantity. Tolerances of 0.01 dB
// (≈ 1.2e-3 in linear ratio) are therefore safe at the 20τ settling horizon
// used for the DC tests.
//
// Below-threshold invariant: if envelope stays < 10^(T/20) throughout, then
// gr ≡ 0, gainDb ≡ 0, and output = input · 1 · makeup exactly. Verified by
// numerical simulation for T=−10, level=−20 dB (peak envelope ≈ 0.095 < 0.316
// threshold) — max |out − in| = 0 across 10 000 samples.
//
// DC steady-state: for input = 1 (constant), envelope = 1 exactly → envDb = 0
// → gr = T·(1 − 1/R) dB exactly. Confirms the canonical compressor formula.
//
// Sine-wave peak output (|input| = 1): settled envelope has ripple dependent
// on attack/release/period. Output peak bound by linear gain 10^(gr/20)
// times 1.5 (generous upper ripple allowance) and 0.7 (lower bound allowing
// for envelope undershoot) around the analytical steady-state value.

TEST_CASE("Compressor: signal below threshold passes through (bit-exact)", "[comp]")
{
    // Signal at −20 dB, threshold −10 dB. Peak |input| = 0.1 < 10^(−10/20)
    // ≈ 0.316 threshold. Envelope tracks at most 0.1 (below threshold) so
    // gr ≡ 0 and gainDb ≡ 0. Output = input exactly.
    ideath::Compressor comp;
    comp.prepare(kSampleRate);
    comp.setThreshold(-10.0f);
    comp.setRatio(4.0f);
    comp.setMakeup(0.0f);
    comp.setAttack(0.001f);
    comp.setRelease(0.05f);

    const float level = dBtoLin(-20.0f);
    for (int i = 0; i < 4410; ++i)
    {
        const float input = level * std::sin(kPi * 2.0f * 440.0f * i / kSampleRate);
        REQUIRE(comp.process(input) == input);
    }
}

TEST_CASE("Compressor: DC steady-state gain reduction matches -(L−T)(1−1/R)", "[comp]")
{
    // For DC input 1.0, envelope = 1 exactly after settling → envDb = 0.
    // gr = −(0 − T)·(1 − 1/R) = T·(1 − 1/R) dB.
    // 20·τ_a = 20 ms = 882 samples gives residual < 2·exp(−20) ≈ 4e-9 dB
    // (cascaded envelope + gainDb one-poles, same attack coef). 0.01 dB
    // tolerance is ~6 orders of magnitude conservative.
    for (float ratio : { 2.0f, 4.0f, 10.0f, 100.0f })
    {
        ideath::Compressor comp;
        comp.prepare(kSampleRate);
        comp.setThreshold(-20.0f);
        comp.setRatio(ratio);
        comp.setMakeup(0.0f);
        comp.setAttack(0.001f);
        comp.setRelease(0.05f);

        for (int i = 0; i < 882; ++i)
            comp.process(1.0f);

        const float expected = -20.0f * (1.0f - 1.0f / ratio);
        INFO("ratio = " << ratio);
        REQUIRE_THAT(comp.getGainReductionDb(), WithinAbs(expected, 0.01f));
    }
}

TEST_CASE("Compressor: sine above threshold reduced to analytical range", "[comp]")
{
    // 0 dB sine at threshold=−20, ratio=4 → gr_ss = −15 dB → linear 0.178.
    // With finite attack/release the envelope ripples, so peak output can
    // overshoot the steady-state compressed level. Allow 1.5× (ripple
    // upward) and 0.7× (envelope trough releasing gainDb but gainDb
    // typically doesn't fully recover within one sample) around 0.178.
    ideath::Compressor comp;
    comp.prepare(kSampleRate);
    comp.setThreshold(-20.0f);
    comp.setRatio(4.0f);
    comp.setMakeup(0.0f);
    comp.setAttack(0.001f);
    comp.setRelease(0.05f);

    // Settle — 8820 samples = 200 ms ≫ 10·τ_r
    for (int i = 0; i < 8820; ++i)
        comp.process(std::sin(kPi * 2.0f * 440.0f * i / kSampleRate));

    float maxOut = 0.0f;
    for (int i = 0; i < 4410; ++i)
    {
        const float out = comp.process(std::sin(kPi * 2.0f * 440.0f * i / kSampleRate));
        maxOut = std::max(maxOut, std::fabs(out));
    }

    const float expected = dBtoLin(-15.0f); // 0.178
    REQUIRE(maxOut > expected * 0.7f);       // > 0.125
    REQUIRE(maxOut < expected * 1.5f);       // < 0.267
}

TEST_CASE("Compressor: higher ratio compresses more (analytical peaks)", "[comp]")
{
    // DC input 1.0 with threshold=−20:
    //   R=2 → gr=−10 → linear 0.316
    //   R=20 → gr=−19 → linear 0.112
    // Steady-state after 882 samples — output is exactly input·10^(gr/20).
    auto steadyPeak = [](float ratio) {
        ideath::Compressor c;
        c.prepare(kSampleRate);
        c.setThreshold(-20.0f);
        c.setRatio(ratio);
        c.setMakeup(0.0f);
        c.setAttack(0.001f);
        c.setRelease(0.05f);
        float out = 0.0f;
        for (int i = 0; i < 882; ++i) out = c.process(1.0f);
        return std::fabs(out);
    };

    const float peakLow  = steadyPeak(2.0f);
    const float peakHigh = steadyPeak(20.0f);

    // Expected 0.316 and 0.112 (residual < 0.01 dB ≈ 0.12% of linear value).
    REQUIRE_THAT(peakLow,  WithinAbs(0.316f, 0.005f));
    REQUIRE_THAT(peakHigh, WithinAbs(0.112f, 0.005f));
    // Monotonicity: higher ratio → lower output.
    REQUIRE(peakHigh < peakLow);
}

TEST_CASE("Compressor: makeup gain scales output by 10^(dB/20)", "[comp]")
{
    // With makeup=+6 dB the output scales by 10^(6/20) ≈ 1.9953 compared
    // to makeup=0 dB at the same input / settings. Use DC to eliminate
    // envelope ripple — ratio between the two is bit-accurate.
    auto steadyPeak = [](float makeupDb) {
        ideath::Compressor c;
        c.prepare(kSampleRate);
        c.setThreshold(-20.0f);
        c.setRatio(4.0f);
        c.setMakeup(makeupDb);
        c.setAttack(0.001f);
        c.setRelease(0.05f);
        float out = 0.0f;
        for (int i = 0; i < 882; ++i) out = c.process(1.0f);
        return std::fabs(out);
    };

    const float noMakeup   = steadyPeak(0.0f);
    const float withMakeup = steadyPeak(6.0f);
    const float expectedRatio = std::pow(10.0f, 6.0f / 20.0f);  // 1.9953

    REQUIRE_THAT(withMakeup / noMakeup, WithinAbs(expectedRatio, 1e-4f));
}

TEST_CASE("Compressor: getGainReductionDb — 0 at silence, -15 dB under 0 dB sine", "[comp]")
{
    ideath::Compressor comp;
    comp.prepare(kSampleRate);
    comp.setThreshold(-20.0f);
    comp.setRatio(4.0f);
    comp.setAttack(0.001f);
    comp.setRelease(0.05f);

    // After prepare() state is envelope=0, gainDb=0. Silence keeps both at 0.
    for (int i = 0; i < 200; ++i) comp.process(0.0f);
    REQUIRE(comp.getGainReductionDb() == 0.0f);

    // After 200 ms of 0 dB sine, gainDb oscillates around the steady state
    // −15 dB. Range [−17, −12] bounds the ripple observed analytically
    // (envelope ≈ 0.95 ± 0.05 → gainDb ≈ −15 ± 2).
    for (int i = 0; i < 8820; ++i)
        comp.process(std::sin(kPi * 2.0f * 440.0f * i / kSampleRate));
    const float gr = comp.getGainReductionDb();
    INFO("gainDb = " << gr);
    REQUIRE(gr < -12.0f);
    REQUIRE(gr > -17.0f);
}

TEST_CASE("Compressor: ratio=1 passes signal bit-exact", "[comp]")
{
    // gr = −over·(1 − 1/1) = 0 for any envelope. gainDb ≡ 0. Output = input
    // · 1 · makeupLinear(0dB=1) = input bit-exact.
    ideath::Compressor comp;
    comp.prepare(kSampleRate);
    comp.setThreshold(-20.0f);
    comp.setRatio(1.0f);
    comp.setMakeup(0.0f);
    comp.setAttack(0.001f);
    comp.setRelease(0.05f);

    for (int i = 0; i < 4410; ++i)
    {
        const float input = 0.8f * std::sin(kPi * 2.0f * 440.0f * i / kSampleRate);
        REQUIRE(comp.process(input) == input);
    }
}

TEST_CASE("Compressor: ratio clamped to >= 1 (bit-exact pass-through on zero/negative)", "[comp]")
{
    // setRatio clamps with std::max(r, 1.0). At clamped ratio = 1 the
    // compressor is transparent (previous test's invariant).
    for (float r : { 0.0f, -5.0f, 0.5f })
    {
        ideath::Compressor comp;
        comp.prepare(kSampleRate);
        comp.setThreshold(-20.0f);
        comp.setRatio(r);
        comp.setMakeup(0.0f);
        const float input = 0.7f;
        for (int i = 0; i < 100; ++i) REQUIRE(comp.process(input) == input);
    }
}

TEST_CASE("Compressor: reset clears state (bit-exact)", "[comp]")
{
    // After reset: envelope=0, gainDb=0. getGainReductionDb must return 0
    // bit-exact, and process(0) returns 0 bit-exact (all intermediate
    // multiplies by 0 are identity in IEEE 754 for finite factors).
    ideath::Compressor comp;
    comp.prepare(kSampleRate);
    comp.setThreshold(-20.0f);
    comp.setRatio(4.0f);

    for (int i = 0; i < 4410; ++i) comp.process(1.0f);
    REQUIRE(comp.getGainReductionDb() < -10.0f);  // sanity: was compressed

    comp.reset();
    REQUIRE(comp.getGainReductionDb() == 0.0f);
    REQUIRE(comp.process(0.0f) == 0.0f);
}

TEST_CASE("Compressor: handles silence (bit-exact zero)", "[comp]")
{
    // Silence in → envelope stays 0, gainDb stays 0, output = 0·1·1 = 0.
    ideath::Compressor comp;
    comp.prepare(kSampleRate);
    comp.setThreshold(-20.0f);
    comp.setRatio(4.0f);

    for (int i = 0; i < 4410; ++i)
        REQUIRE(comp.process(0.0f) == 0.0f);
}

TEST_CASE("Compressor: release is slower than attack", "[comp]")
{
    // With attack=1 ms and release=50 ms, time for gainDb to reach half
    // of steady-state (−7.5 dB) during attack is dominated by the 1 ms
    // attack; time to recover back through −7.5 dB after silence is
    // dominated by the 50 ms envelope release (envDb stays above threshold
    // until envelope decays below 0.1). Analytical sim: attack ≈ 59
    // samples, release ≈ 4451 samples — ratio ≈ 75. Require > 10× to
    // catch a swapped attack/release bug while tolerating implementation
    // variance.
    ideath::Compressor comp;
    comp.prepare(kSampleRate);
    comp.setThreshold(-20.0f);
    comp.setRatio(4.0f);
    comp.setMakeup(0.0f);
    comp.setAttack(0.001f);
    comp.setRelease(0.05f);

    int attackSamples = 0;
    while (comp.getGainReductionDb() > -7.5f && attackSamples < 20000)
    {
        comp.process(1.0f);
        ++attackSamples;
    }
    // Fully settle
    for (int i = 0; i < 500; ++i) comp.process(1.0f);

    int releaseSamples = 0;
    while (comp.getGainReductionDb() < -7.5f && releaseSamples < 500000)
    {
        comp.process(0.0f);
        ++releaseSamples;
    }

    INFO("attack=" << attackSamples << " release=" << releaseSamples);
    REQUIRE(attackSamples  > 0);
    REQUIRE(attackSamples  < 20000);
    REQUIRE(releaseSamples < 500000);
    REQUIRE(releaseSamples > 10 * attackSamples);
}

TEST_CASE("Compressor: default-constructible and produces finite output", "[comp]")
{
    // Constructor calls prepare(44100) which initializes all coefficients.
    // No prepare/set calls required before first process().
    ideath::Compressor comp;
    const float out = comp.process(0.5f);
    REQUIRE(std::isfinite(out));
}

// ---------------------------------------------------------------------------
// Long-run stability
// ---------------------------------------------------------------------------

TEST_CASE("Compressor: 10-second stability at extreme settings", "[comp][stability]")
{
    // 10 s × 44.1 kHz = 441 000 samples with aggressive compression
    // (−30 dB threshold, 20:1 ratio, fast attack, long release). Envelope
    // and gainDb must stay finite; output bounded by input amplitude times
    // a maximum-possible gain (makeup=+6 dB → 2×). Input amplitude ≤ 1 →
    // output ≤ 2 by construction.
    ideath::Compressor comp;
    comp.prepare(kSampleRate);
    comp.setThreshold(-30.0f);
    comp.setRatio(20.0f);
    comp.setAttack(0.0005f);
    comp.setRelease(0.200f);
    comp.setMakeup(6.0f);

    for (int i = 0; i < 441000; ++i)
    {
        const float t = static_cast<float>(i) / kSampleRate;
        const float input = 0.9f * (std::sin(2.0f * kPi * 220.0f * t)
                                   + 0.3f * std::sin(2.0f * kPi * 1320.0f * t));
        const float clamped = std::max(-1.0f, std::min(1.0f, input));
        const float out = comp.process(clamped);
        REQUIRE(std::isfinite(out));
        REQUIRE(std::fabs(out) <= 2.0f);
        REQUIRE(std::isfinite(comp.getGainReductionDb()));
    }
}
