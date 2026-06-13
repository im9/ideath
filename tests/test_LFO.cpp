#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/LFO.h>
#include <cmath>
#include <set>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

// Threshold derivations used throughout this file
// -----------------------------------------------
// Analytical waveform ranges (all bipolar [-1, 1], phase ∈ [0, 1)):
//   waveSine(φ)     = sin(2π φ)              range [-1, 1]
//   waveTriangle(φ) = 4φ − 1  (φ<0.5)        range [-1, 1]
//                   = 3 − 4φ  (φ≥0.5)        range [-1, 1]
//   waveSaw(φ)      = 2φ − 1                 range [-1, 1]
//   waveSquare(φ)   = ±1.0f literal          range {−1, +1}
//
// Unipolar conversion: (out + 1) * 0.5 → [0, 1].
//
// Shape: carrier' = (1 − s/2)·carrier + (s/2)·second, both inputs in [-1, 1]
//        → |carrier'| ≤ 1 for all shape ∈ [0, 1].
//
// Curve morph anchors: 0=sine, 1/3=triangle, 2/3=saw, 1=square (the branch
// parameter t hits exactly 0 and 1 at anchor boundaries → no cross-fade bleed).
//
// Phase accumulation at rate R on sampleRate SR: phaseInc = R/SR in float.
// Over N samples the accumulated phase is N · phaseInc_float which is not
// bit-exact = N·R/SR — there is float drift of a few ULP per step, so phase
// wraps happen near but not exactly at analytical cycle boundaries.

TEST_CASE("LFO: sine bipolar output in [-1, 1]", "[lfo]")
{
    ideath::LFO lfo;
    lfo.prepare(kSampleRate);
    lfo.setRate(5.0f);
    lfo.setWaveform(ideath::LFO::Waveform::Sine);
    lfo.setPolarity(ideath::LFO::Polarity::Bipolar);

    // std::sin always returns a value in [-1, 1] by IEEE-754 spec; the
    // bounds are hard, no tolerance needed.
    for (int i = 0; i < 44100; ++i)
    {
        float s = lfo.process();
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
    }
}

TEST_CASE("LFO: unipolar output in [0, 1]", "[lfo]")
{
    ideath::LFO lfo;
    lfo.prepare(kSampleRate);
    lfo.setRate(5.0f);
    lfo.setWaveform(ideath::LFO::Waveform::Triangle);
    lfo.setPolarity(ideath::LFO::Polarity::Unipolar);

    // Triangle ∈ [-1, 1] exactly, (x+1)*0.5 ∈ [0, 1] exactly. The lower
    // bound is 0 at phase=0.25, upper is 1 at phase=0 and 1 at phase=0.5.
    for (int i = 0; i < 44100; ++i)
    {
        float s = lfo.process();
        REQUIRE(s >= 0.0f);
        REQUIRE(s <= 1.0f);
    }
}

TEST_CASE("LFO: square produces two levels", "[lfo]")
{
    ideath::LFO lfo;
    lfo.prepare(kSampleRate);
    lfo.setRate(10.0f);
    lfo.setWaveform(ideath::LFO::Waveform::Square);
    lfo.setPolarity(ideath::LFO::Polarity::Bipolar);

    // waveSquare returns the *literal* ±1.0f — no arithmetic, so the set
    // of observed values is exactly {−1, +1} (size 2).
    std::set<float> values;
    for (int i = 0; i < 44100; ++i)
        values.insert(lfo.process());

    REQUIRE(values.size() == 2);
}

TEST_CASE("LFO: saw ramps up", "[lfo]")
{
    ideath::LFO lfo;
    lfo.prepare(kSampleRate);
    lfo.setRate(1.0f);
    lfo.setWaveform(ideath::LFO::Waveform::Saw);
    lfo.setPolarity(ideath::LFO::Polarity::Bipolar);

    // Saw(φ) = 2φ − 1 is strictly increasing in φ between wraps, dropping
    // by exactly 2.0 at each phase wrap. In `samplesPerCycle` process()
    // calls at rate 1 Hz, phase traverses exactly one cycle, so there is
    // at most one wrap — giving rising ≥ (samplesPerCycle − 1) − 1.
    // (Due to float drift, the wrap may land just outside this window, in
    // which case rising = samplesPerCycle − 1 exactly.)
    int rising = 0;
    float prev = lfo.process();
    int samplesPerCycle = static_cast<int>(kSampleRate / 1.0f);
    for (int i = 1; i < samplesPerCycle; ++i)
    {
        float s = lfo.process();
        if (s > prev) ++rising;
        prev = s;
    }

    REQUIRE(rising >= samplesPerCycle - 2);
}

TEST_CASE("LFO: sample-and-hold changes once per cycle", "[lfo]")
{
    ideath::LFO lfo;
    lfo.prepare(kSampleRate);
    lfo.setRate(10.0f);
    lfo.setWaveform(ideath::LFO::Waveform::SampleAndHold);
    lfo.setPolarity(ideath::LFO::Polarity::Bipolar);

    // S&H updates holdValue_ only on phase wrap (phase_ < prevPhase_).
    // At 10 Hz over 1 s = 10 analytical cycles; due to phaseInc_float drift
    // the count may be 9 or 10 wraps over exactly 44100 process() calls.
    // Each wrap produces a new xorshift noise value, which differs from
    // the previous with probability 1 − 2^-32 (negligibly less than 1).
    int changes = 0;
    float prev = lfo.process();
    for (int i = 1; i < 44100; ++i)
    {
        float s = lfo.process();
        if (s != prev) ++changes;
        prev = s;
    }

    REQUIRE(changes >= 9);
    REQUIRE(changes <= 11);
}

TEST_CASE("LFO: one-shot stops after one cycle", "[lfo]")
{
    ideath::LFO lfo;
    lfo.prepare(kSampleRate);
    lfo.setRate(10.0f); // 10 Hz → 1 cycle = 4410 samples
    lfo.setWaveform(ideath::LFO::Waveform::Sine);
    lfo.setPolarity(ideath::LFO::Polarity::Bipolar);
    lfo.setOneShot(true);
    lfo.trigger();

    REQUIRE_FALSE(lfo.isFinished());

    // 5000 samples > 4410 → at least one wrap, so one-shot completion
    // fires and the latched holdValue_ is written.
    for (int i = 0; i < 5000; ++i)
        lfo.process();

    REQUIRE(lfo.isFinished());

    // Finished branch returns holdValue_ as a literal each call — exact
    // equality; tolerance is for WithinAbs API consistency only.
    float held = lfo.process();
    for (int i = 0; i < 100; ++i)
        REQUIRE_THAT(lfo.process(), WithinAbs(held, 1e-6f));
}

TEST_CASE("LFO: trigger resets phase", "[lfo]")
{
    ideath::LFO lfo;
    lfo.prepare(kSampleRate);
    lfo.setRate(5.0f);
    lfo.setWaveform(ideath::LFO::Waveform::Sine);

    for (int i = 0; i < 10000; ++i)
        lfo.process();

    REQUIRE(lfo.getPhase() > 0.0f);
    lfo.trigger();
    // trigger() assigns phase_ = 0.0f literal.
    REQUIRE_THAT(lfo.getPhase(), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("LFO: reset preserves rate", "[lfo]")
{
    // reset() clears phase/hold state but must preserve phaseInc (rate setting).
    // Matches filter/envelope convention: reset = zero state, not unconfigured.
    // Regression guard for d79fa88.
    ideath::LFO lfo;
    lfo.prepare(kSampleRate);
    lfo.setRate(5.0f);
    lfo.setWaveform(ideath::LFO::Waveform::Sine);

    for (int i = 0; i < 10000; ++i)
        lfo.process();

    lfo.reset();

    ideath::LFO fresh;
    fresh.prepare(kSampleRate);
    fresh.setRate(5.0f);
    fresh.setWaveform(ideath::LFO::Waveform::Sine);

    // Both LFOs have phase_=0, phase2_=0, same phaseInc_. Each process()
    // is pure float arithmetic with no randomness (Sine path), so outputs
    // are bit-identical. 1e-6 is WithinAbs conventional tolerance.
    for (int i = 0; i < 16; ++i)
    {
        float a = lfo.process();
        float b = fresh.process();
        REQUIRE_THAT(a, WithinAbs(b, 1e-6f));
    }
}

TEST_CASE("LFO: reset restores deterministic S&H noise state", "[lfo]")
{
    // S&H uses an internal xorshift32 RNG (noiseState_) seeded in the header.
    // reset() must restore that seed so a recorded → reset → replay sequence
    // produces bit-identical output (same musical contract as Sine in the
    // "reset preserves rate" test above).  Before this guard, noiseState_
    // continued advancing across reset(), so replay differed from initial run.
    ideath::LFO a;
    a.prepare(kSampleRate);
    a.setRate(20.0f);
    a.setWaveform(ideath::LFO::Waveform::SampleAndHold);
    a.setPolarity(ideath::LFO::Polarity::Bipolar);

    constexpr int N = 8192; // > 8 wraps at 20 Hz → many S&H updates captured

    std::vector<float> first(N);
    for (int i = 0; i < N; ++i)
        first[i] = a.process();

    a.reset();
    for (int i = 0; i < N; ++i)
    {
        // xorshift32 is deterministic given seed; reset() must restore the
        // seed so replay matches bit-for-bit (1e-6 is API tolerance only).
        float v = a.process();
        REQUIRE_THAT(v, WithinAbs(first[i], 1e-6f));
    }
}

// ---------------------------------------------------------------------------
// Shape, Curve, Quantize
// ---------------------------------------------------------------------------

TEST_CASE("LFO: defaults equivalent to legacy sine", "[lfo][adr009]")
{
    // With Shape, Curve, Quantize all at default 0, process() takes exactly
    // the legacy sine branch — the same std::sin call on the same phase_ —
    // so outputs must be bit-identical.
    ideath::LFO a, b;
    a.prepare(kSampleRate);
    b.prepare(kSampleRate);
    a.setRate(7.5f);
    b.setRate(7.5f);
    a.setWaveform(ideath::LFO::Waveform::Sine);
    b.setWaveform(ideath::LFO::Waveform::Sine);

    for (int i = 0; i < 4096; ++i)
        REQUIRE(a.process() == b.process());
}

TEST_CASE("LFO: Shape sweep stays bounded and finite", "[lfo][adr009]")
{
    ideath::LFO lfo;
    lfo.prepare(kSampleRate);
    lfo.setRate(3.0f);

    // Shape mixes carrier and second via (1-m)·a + m·b with m = shape/2
    // ∈ [0, 0.5] and |a|,|b| ≤ 1. Convex combination ⇒ |output| ≤ 1
    // exactly. Use 1 + 1e-5 to absorb 1 ULP of float rounding on the mix.
    for (int step = 0; step <= 20; ++step)
    {
        lfo.setShape(static_cast<float>(step) / 20.0f);
        for (int i = 0; i < 2048; ++i)
        {
            float s = lfo.process();
            REQUIRE(std::isfinite(s));
            REQUIRE(s >= -1.0f - 1e-5f);
            REQUIRE(s <= 1.0f + 1e-5f);
        }
    }
}

TEST_CASE("LFO: Shape clamps out-of-range input", "[lfo][adr009]")
{
    ideath::LFO lfo;
    lfo.prepare(kSampleRate);
    lfo.setRate(2.0f);

    lfo.setShape(-5.0f);  // std::clamp → 0 → single-oscillator
    for (int i = 0; i < 1024; ++i)
        REQUIRE(std::isfinite(lfo.process()));

    lfo.setShape(99.0f);  // std::clamp → 1
    for (int i = 0; i < 1024; ++i)
        REQUIRE(std::isfinite(lfo.process()));
}

TEST_CASE("LFO: Curve at 0 matches sine, at 1 matches square", "[lfo][adr009]")
{
    auto runFor = [](float curve) {
        ideath::LFO lfo;
        lfo.prepare(kSampleRate);
        lfo.setRate(1.0f);
        lfo.setCurve(curve);
        std::vector<float> samples;
        samples.reserve(static_cast<size_t>(kSampleRate));
        for (int i = 0; i < static_cast<int>(kSampleRate); ++i)
            samples.push_back(lfo.process());
        return samples;
    };

    auto sineRef = runFor(0.0f);
    // Curve == 0 routes to the legacy Sine branch (default Waveform). The
    // i-th returned sample corresponds to phase ≈ (i+1)/N (process advances
    // phase before sampling). Float phase accumulation drifts ~i · ULP per
    // step, so after 11000 samples the drift in sin output is bounded by
    // 2π · 11000 · 1e-8 ≈ 7e-4. 1e-3 covers this with margin.
    for (size_t i = 0; i < sineRef.size() / 4; i += 50)
    {
        const float phase = static_cast<float>(i + 1) / kSampleRate;
        const float expected = std::sin(2.0f * static_cast<float>(M_PI) * phase);
        REQUIRE_THAT(sineRef[i], WithinAbs(expected, 1e-3f));
    }

    // Curve == 1.0 → morph hits waveSquare only (third branch with t=1).
    // Square is +1 for φ<0.5 and −1 for φ≥0.5, so over an integer number
    // of cycles we get N/2 samples at each extreme (±1 within 1 ULP).
    // Exact counts drift by a few samples due to phase wrap timing.
    auto squareWave = runFor(1.0f);
    int aboveZero = 0, belowZero = 0;
    for (float s : squareWave)
    {
        if (s > 0.99f) ++aboveZero;
        else if (s < -0.99f) ++belowZero;
    }
    // Expect N/2 = 22050 each; allow ±100 samples for wrap-timing drift
    // (observed: 22047 / 22053 at N=44100).
    const int half = static_cast<int>(squareWave.size()) / 2;
    REQUIRE(aboveZero >= half - 100);
    REQUIRE(belowZero >= half - 100);
}

TEST_CASE("LFO: Curve at 0.66 has clear ramp character", "[lfo][adr009]")
{
    // curve = 2/3 lands at the saw anchor (second branch, t=1): morph
    // = waveSaw exactly. Saw is strictly increasing between wraps, with
    // a single wrap per cycle.
    ideath::LFO lfo;
    lfo.prepare(kSampleRate);
    lfo.setRate(1.0f);
    lfo.setCurve(2.0f / 3.0f);

    int rising = 0, total = 0;
    float prev = lfo.process();
    const int samplesPerCycle = static_cast<int>(kSampleRate);
    for (int i = 1; i < samplesPerCycle; ++i)
    {
        float s = lfo.process();
        if (s > prev) ++rising;
        ++total;
        prev = s;
    }
    // At most one wrap in the window → rising ≥ total − 1. Use −2 to
    // absorb any float-precision edge case where s==prev.
    REQUIRE(rising >= total - 2);
}

TEST_CASE("LFO: Curve clamps out-of-range input", "[lfo][adr009]")
{
    ideath::LFO lfo;
    lfo.prepare(kSampleRate);
    lfo.setRate(5.0f);
    lfo.setCurve(-2.0f);   // std::clamp → 0
    for (int i = 0; i < 512; ++i)
        REQUIRE(std::isfinite(lfo.process()));
    lfo.setCurve(99.0f);   // std::clamp → 1
    for (int i = 0; i < 512; ++i)
        REQUIRE(std::isfinite(lfo.process()));
}

TEST_CASE("LFO: Quantize=1 holds value across each cycle", "[lfo][adr009]")
{
    ideath::LFO lfo;
    lfo.prepare(kSampleRate);
    lfo.setRate(2.0f);  // 0.5 s cycle = 22050 samples per step
    lfo.setQuantize(1.0f);

    // At quantize=1, out = quantizeHold_ exactly (the lerp collapses).
    // quantizeHold_ updates only on phase wrap. Between wraps every
    // output equals the held value bit-exact. Step length is 22050
    // samples; within a 20000-sample window after the first captured
    // value we never cross a wrap, so all 20000 comparisons return
    // `s == prev` and run length equals 20001.
    lfo.process();
    int maxRun = 0;
    int run = 1;
    float prev = lfo.process();
    for (int i = 0; i < 20000; ++i)
    {
        float s = lfo.process();
        if (s == prev) ++run;
        else { if (run > maxRun) maxRun = run; run = 1; }
        prev = s;
    }
    if (run > maxRun) maxRun = run;
    // Expected 20001; allow off-by-one for wrap-timing edge cases.
    REQUIRE(maxRun >= 20000);
}

TEST_CASE("LFO: Quantize clamps out-of-range input", "[lfo][adr009]")
{
    ideath::LFO lfo;
    lfo.prepare(kSampleRate);
    lfo.setRate(5.0f);
    lfo.setQuantize(-3.0f);   // std::clamp → 0
    for (int i = 0; i < 512; ++i)
        REQUIRE(std::isfinite(lfo.process()));
    lfo.setQuantize(42.0f);   // std::clamp → 1
    for (int i = 0; i < 512; ++i)
        REQUIRE(std::isfinite(lfo.process()));
}

TEST_CASE("LFO: sine DC offset near zero", "[lfo]")
{
    ideath::LFO lfo;
    lfo.prepare(kSampleRate);
    lfo.setRate(10.0f);
    lfo.setWaveform(ideath::LFO::Waveform::Sine);
    lfo.setPolarity(ideath::LFO::Polarity::Bipolar);

    // ∫ sin(2π f t) dt over an integer number of cycles is 0 analytically.
    // Here N=44100 samples covers 10 nominal cycles. phaseInc_float =
    // 10.0f/44100.0f is not exact, so the summation has a residual phase
    // error. Observed DC: ~3.3e-5 at N=44100. Use 5e-4 as the tolerance
    // (15× observed) to stay robust across compiler ULP differences while
    // still 20× tighter than the old 0.01 guard.
    double sum = 0.0;
    constexpr int N = 44100;
    for (int i = 0; i < N; ++i)
        sum += static_cast<double>(lfo.process());

    float dc = static_cast<float>(sum / N);
    REQUIRE_THAT(dc, WithinAbs(0.0f, 5e-4f));
}

// ---------------------------------------------------------------------------
// Long-run stability and extreme parameter coverage
// ---------------------------------------------------------------------------

TEST_CASE("LFO: 10-second phase stability", "[lfo][stability]")
{
    // 10 s × 44.1 kHz = 441 000 process() calls. Each call adds phaseInc_
    // and wraps via phase -= std::floor(phase). The wrap confines phase_
    // to [0, 1) indefinitely, so output stays bounded regardless of how
    // much float drift accumulates.
    ideath::LFO lfo;
    lfo.prepare(kSampleRate);
    lfo.setRate(2.5f);
    lfo.setShape(0.4f);
    lfo.setCurve(0.3f);
    lfo.setQuantize(0.2f);

    constexpr int N = 441000;
    for (int i = 0; i < N; ++i)
    {
        float s = lfo.process();
        REQUIRE(std::isfinite(s));
        // Shape lerp bound plus quantize hold replays a prior carrier
        // value, so output stays in [-1, 1] within 1 ULP.
        REQUIRE(s >= -1.0f - 1e-5f);
        REQUIRE(s <= 1.0f + 1e-5f);
    }
    // Phase must still be in [0, 1) after 10 s of drift.
    REQUIRE(lfo.getPhase() >= 0.0f);
    REQUIRE(lfo.getPhase() < 1.0f);
}

TEST_CASE("LFO: extreme parameter combinations stay bounded", "[lfo][stability]")
{
    // Rate = 0: setRate clamps negatives to 0 so phaseInc_ = 0. Phase
    // never advances and output is constant at the initial waveform
    // value (sin(0) = 0 for the default Sine branch).
    {
        ideath::LFO lfo;
        lfo.prepare(kSampleRate);
        lfo.setRate(0.0f);
        for (int i = 0; i < 4096; ++i)
            REQUIRE_THAT(lfo.process(), WithinAbs(0.0f, 1e-6f));
    }

    // Rate = negative: setRate clamps to 0, same as above — just
    // confirming no NaN/inf from a negative input.
    {
        ideath::LFO lfo;
        lfo.prepare(kSampleRate);
        lfo.setRate(-10.0f);
        for (int i = 0; i < 1024; ++i)
            REQUIRE(std::isfinite(lfo.process()));
    }

    // Rate at audio rate (1 kHz). phaseInc = 1000/44100 ≈ 0.0227 — about
    // 44 samples per cycle, well below Nyquist, no aliasing mitigation
    // needed (LFO is explicitly unaliased per header comment).
    {
        ideath::LFO lfo;
        lfo.prepare(kSampleRate);
        lfo.setRate(1000.0f);
        lfo.setWaveform(ideath::LFO::Waveform::Sine);
        for (int i = 0; i < 4410; ++i)
        {
            float s = lfo.process();
            REQUIRE(std::isfinite(s));
            REQUIRE(s >= -1.0f);
            REQUIRE(s <= 1.0f);
        }
    }

    // All three morph params (Shape / Curve / Quantize) at maximum simultaneously. Shape mixes
    // two morphs, Quantize holds the result once per cycle. Output
    // bound is still [-1, 1] (lerp of convex-combined values).
    {
        ideath::LFO lfo;
        lfo.prepare(kSampleRate);
        lfo.setRate(5.0f);
        lfo.setShape(1.0f);
        lfo.setCurve(1.0f);
        lfo.setQuantize(1.0f);
        for (int i = 0; i < 44100; ++i)
        {
            float s = lfo.process();
            REQUIRE(std::isfinite(s));
            REQUIRE(s >= -1.0f - 1e-5f);
            REQUIRE(s <= 1.0f + 1e-5f);
        }
    }

    // Unipolar polarity under extreme params: output must stay in
    // [0, 1] (shifted convex combination).
    {
        ideath::LFO lfo;
        lfo.prepare(kSampleRate);
        lfo.setRate(3.0f);
        lfo.setShape(0.8f);
        lfo.setCurve(0.5f);
        lfo.setQuantize(0.5f);
        lfo.setPolarity(ideath::LFO::Polarity::Unipolar);
        for (int i = 0; i < 44100; ++i)
        {
            float s = lfo.process();
            REQUIRE(std::isfinite(s));
            REQUIRE(s >= 0.0f - 1e-5f);
            REQUIRE(s <= 1.0f + 1e-5f);
        }
    }
}
