#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/FunctionGenerator.h>
#include <algorithm>
#include <cmath>

using Catch::Matchers::WithinAbs;

// ---------------------------------------------------------------------------
// Threshold derivation notes (used throughout this file)
//
// Phase increment math (canonical for every timing test below):
//     phaseInc = 1 / max(1, time_seconds * sample_rate)
// Starting from phase = 0, the segment completes when the cumulative phase
// reaches 1.0, i.e. exactly N = time_seconds * sample_rate process() calls
// later. The implementation advances phase BEFORE computing the sample, so
// the peak (output = 1.0) lands on call number N (0-indexed sample N-1),
// and the trough (output = 0.0) at the end of fall lands one segment-length
// later. "± 1 sample" therefore accommodates the off-by-one between
// "samples elapsed" (N) and "sample index" (N-1) plus any float-rounding
// drift from accumulating `phaseInc` across thousands of samples.
//
// Curve math:
//     curveExponent = 1 + |curve| * k    with k = 2 (impl constant)
//     curve == 0  → exponent = 1.0 (linear, identity)
//     curve = +1  → exponent = 3.0, shape(t) = t^3
//     curve = -1  → exponent = 3.0, shape(t) = 1 - (1-t)^3
// Midpoint values (phase = 0.5):
//     curve =  0: 0.5         (linear)
//     curve = +1: 0.5^3 = 0.125
//     curve = -1: 1 - 0.5^3 = 0.875
// ---------------------------------------------------------------------------

static constexpr float kSampleRate = 44100.0f;

namespace {

// Default-configured FG with a slow-enough rise/fall to make the per-sample
// step small. Used by most timing/continuity tests.
ideath::FunctionGenerator makeFG(float sr = kSampleRate,
                                 float rise = 0.1f,
                                 float fall = 0.1f,
                                 float curve = 0.0f,
                                 bool cycle = false)
{
    ideath::FunctionGenerator fg;
    fg.prepare(sr);
    fg.setRise(rise);
    fg.setFall(fall);
    fg.setCurve(curve);
    fg.setCycle(cycle);
    return fg;
}

} // namespace

// ---------------------------------------------------------------------------
// 0. Initial state
// ---------------------------------------------------------------------------

TEST_CASE("FunctionGenerator: starts idle and silent", "[fg]")
{
    ideath::FunctionGenerator fg;
    fg.prepare(kSampleRate);
    REQUIRE_FALSE(fg.isActive());
    // Idle branch assigns currentValue_ = 0.0f literal — exact zero.
    REQUIRE_THAT(fg.process(), WithinAbs(0.0f, 1e-7f));
    REQUIRE_FALSE(fg.consumeEoc());
}

// ---------------------------------------------------------------------------
// 1. Rise time accuracy — spec test #1
// ---------------------------------------------------------------------------

TEST_CASE("FunctionGenerator: rise time hits peak within 1 sample", "[fg][timing]")
{
    // setRise(0.1) at 44100 Hz → 4410 samples to peak. With phase advanced
    // before sample computation, the peak (output 1.0) lands on call N
    // where N = rise * sr = 4410. Sample index (0-based) = N - 1 = 4409.
    // ± 1 sample tolerance covers any float accumulation in the per-sample
    // sum of 1/4410.
    auto fg = makeFG(kSampleRate, 0.1f, 0.1f, 0.0f);
    fg.trigger();

    const int expected = static_cast<int>(0.1f * kSampleRate); // 4410
    int peakSample = -1;
    for (int i = 0; i < expected * 2; ++i)
    {
        float v = fg.process();
        if (peakSample < 0 && v >= 1.0f - 1e-6f)
        {
            peakSample = i + 1; // count of process() calls until peak
            break;
        }
    }
    REQUIRE(peakSample > 0);
    REQUIRE(std::abs(peakSample - expected) <= 1);
}

// ---------------------------------------------------------------------------
// 2. Fall time accuracy — spec test #2
// ---------------------------------------------------------------------------

TEST_CASE("FunctionGenerator: fall time reaches zero within 1 sample of setFall", "[fg][timing]")
{
    // setRise(0.05) → 2205 samples to peak; setFall(0.1) → 4410 samples to
    // out = 0. Same phase-increment math as rise; the trough (output 0)
    // lands on the call that pushes phase past 1.0, which is exactly
    // (fall * sr) calls after the peak sample.
    auto fg = makeFG(kSampleRate, 0.05f, 0.1f, 0.0f);
    fg.trigger();

    const int riseCount = static_cast<int>(0.05f * kSampleRate); // 2205
    const int fallCount = static_cast<int>(0.10f * kSampleRate); // 4410
    int peakSample = -1;
    int zeroSample = -1;
    for (int i = 0; i < (riseCount + fallCount) * 2; ++i)
    {
        float v = fg.process();
        if (peakSample < 0 && v >= 1.0f - 1e-6f)
            peakSample = i + 1;
        else if (peakSample > 0 && zeroSample < 0
                 && v <= 1e-6f && !fg.isActive())
            zeroSample = i + 1;
    }
    REQUIRE(peakSample > 0);
    REQUIRE(zeroSample > 0);
    // Samples between peak and trough = fall segment length.
    const int fallSpan = zeroSample - peakSample;
    REQUIRE(std::abs(fallSpan - fallCount) <= 1);
}

// ---------------------------------------------------------------------------
// 3. Output bounds — spec test #3
// ---------------------------------------------------------------------------

TEST_CASE("FunctionGenerator: output stays in [0, 1] across curves and cycle modes",
          "[fg][bounds]")
{
    // shape(t) for t ∈ [0, 1]:
    //   curve > 0: t^exponent — strictly monotone in [0, 1].
    //   curve < 0: 1 - (1-t)^exponent — strictly monotone in [0, 1].
    //   curve = 0: identity.
    // All three are mathematically bounded by [0, 1]; float arithmetic on
    // pow() of values ≤ 1 cannot exceed 1 (rounding goes nearest, never
    // outside the unit interval for these inputs). Tolerance 1e-6 is
    // generous numeric headroom.
    for (float curve : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
    {
        for (bool cycle : { false, true })
        {
            auto fg = makeFG(kSampleRate, 0.05f, 0.07f, curve, cycle);
            if (!cycle) fg.trigger();
            // 1 s = 44100 samples covers at least 4 full cycles in cycle
            // mode (0.12 s period) and the full one-shot in non-cycle.
            for (int i = 0; i < 44100; ++i)
            {
                float v = fg.process();
                REQUIRE(std::isfinite(v));
                REQUIRE(v >= 0.0f - 1e-6f);
                REQUIRE(v <= 1.0f + 1e-6f);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 4. Linear curve midpoint — spec test #4
// ---------------------------------------------------------------------------

TEST_CASE("FunctionGenerator: linear curve passes through 0.5 at rise midpoint",
          "[fg][curve]")
{
    // setRise(0.1) → 4410 sample rise. At sample N = 2205 (half), phase
    // accumulator = 2205/4410 = 0.5 exactly when expressed as a sum of
    // (1/4410). Float drift over 2205 additions of 1/4410 stays below
    // 2205 ULP at the 0.5 magnitude (~ 2.6e-4), so 1e-3 is comfortable.
    auto fg = makeFG(kSampleRate, 0.1f, 0.1f, 0.0f);
    fg.trigger();
    float v = 0.0f;
    for (int i = 0; i < 2205; ++i)
        v = fg.process();
    REQUIRE_THAT(v, WithinAbs(0.5f, 1e-3f));
}

// ---------------------------------------------------------------------------
// 5. Curve sign correctness — spec test #5
// ---------------------------------------------------------------------------

TEST_CASE("FunctionGenerator: positive curve bends midpoint downward",
          "[fg][curve]")
{
    // curve=+1, kCurveK=2 → exponent=3. shape(0.5) = 0.5^3 = 0.125.
    // Threshold 0.4: passing requires < 0.4, regression to linear would
    // give ≈ 0.5 (clearly above). Gap of 0.275 keeps a wide margin even
    // for moderate curve values.
    auto fg = makeFG(kSampleRate, 0.1f, 0.1f, +1.0f);
    fg.trigger();
    float v = 0.0f;
    for (int i = 0; i < 2205; ++i)
        v = fg.process();
    REQUIRE(v < 0.4f);
    // Expected midpoint = 0.125; tolerance 0.05 accommodates the same
    // 2205-step phase drift and the pow() rounding.
    REQUIRE_THAT(v, WithinAbs(0.125f, 0.05f));
}

TEST_CASE("FunctionGenerator: negative curve bends midpoint upward",
          "[fg][curve]")
{
    // curve=-1, kCurveK=2 → exponent=3. shape(0.5) = 1 - 0.5^3 = 0.875.
    // Threshold 0.6 (must exceed): same reasoning as the positive case,
    // mirrored across 0.5.
    auto fg = makeFG(kSampleRate, 0.1f, 0.1f, -1.0f);
    fg.trigger();
    float v = 0.0f;
    for (int i = 0; i < 2205; ++i)
        v = fg.process();
    REQUIRE(v > 0.6f);
    REQUIRE_THAT(v, WithinAbs(0.875f, 0.05f));
}

// ---------------------------------------------------------------------------
// 6. Cycle period — spec test #6
// ---------------------------------------------------------------------------

TEST_CASE("FunctionGenerator: cycle mode period equals rise + fall", "[fg][cycle]")
{
    // setRise(0.05) + setFall(0.05) at 44100 → period = (0.05+0.05) * 44100
    // = 4410 samples. Measure peak-to-peak across two cycles. Each segment
    // contributes its own off-by-one (sample-index vs. count), but those
    // off-by-ones are constant per cycle so peak-to-peak is exact (± 1
    // sample for float drift in the accumulator).
    auto fg = makeFG(kSampleRate, 0.05f, 0.05f, 0.0f, /*cycle=*/true);
    // No trigger needed: setCycle(true) auto-starts from Idle.

    const int expectedPeriod = static_cast<int>((0.05f + 0.05f) * kSampleRate);
    int firstPeak = -1, secondPeak = -1;
    bool wasPeak = false;
    for (int i = 0; i < expectedPeriod * 3; ++i)
    {
        float v = fg.process();
        bool isPeak = v >= 1.0f - 1e-6f;
        if (isPeak && !wasPeak)
        {
            if (firstPeak < 0) firstPeak = i;
            else if (secondPeak < 0) { secondPeak = i; break; }
        }
        wasPeak = isPeak;
    }
    REQUIRE(firstPeak >= 0);
    REQUIRE(secondPeak > firstPeak);
    const int period = secondPeak - firstPeak;
    REQUIRE(std::abs(period - expectedPeriod) <= 1);
}

// ---------------------------------------------------------------------------
// 7. EOC pulse — spec test #7
// ---------------------------------------------------------------------------

TEST_CASE("FunctionGenerator: EOC fires exactly once per one-shot cycle",
          "[fg][eoc]")
{
    // One-shot: rise (4410) + fall (4410) = 8820 samples to EOC. Run 12000
    // samples → EOC must have fired exactly once, and the flag must clear
    // after the first consumeEoc() call.
    auto fg = makeFG(kSampleRate, 0.1f, 0.1f, 0.0f);
    fg.trigger();
    int eocCount = 0;
    for (int i = 0; i < 12000; ++i)
    {
        fg.process();
        if (fg.consumeEoc()) ++eocCount;
    }
    REQUIRE(eocCount == 1);
    // After consume, the flag stays cleared until next fall completion.
    REQUIRE_FALSE(fg.consumeEoc());
}

TEST_CASE("FunctionGenerator: EOC fires once per cycle in cycle mode",
          "[fg][eoc][cycle]")
{
    // Cycle period = 0.05 + 0.05 = 0.1 s = 4410 samples. Over 5 full
    // cycles (22050 samples) we expect 5 EOCs, polled each sample.
    auto fg = makeFG(kSampleRate, 0.05f, 0.05f, 0.0f, /*cycle=*/true);
    int eocCount = 0;
    const int cycles = 5;
    const int samples = static_cast<int>(0.10f * kSampleRate) * cycles;
    for (int i = 0; i < samples; ++i)
    {
        fg.process();
        if (fg.consumeEoc()) ++eocCount;
    }
    REQUIRE(eocCount == cycles);
}

TEST_CASE("FunctionGenerator: EOC survives block-boundary polling", "[fg][eoc]")
{
    // Polling once per 64-sample block (typical audio block) must not lose
    // any EOCs: each cycle = 4410 samples ≫ 64, so within any block at
    // most one fall completion occurs. eocPending_ is a sticky flag —
    // setting it twice in the same block (impossible here) would still
    // surface a single true on the next consumeEoc().
    auto fg = makeFG(kSampleRate, 0.05f, 0.05f, 0.0f, /*cycle=*/true);
    int eocCount = 0;
    const int block = 64;
    const int blocks = 200;          // 12800 samples ≈ 2.9 cycles
    for (int b = 0; b < blocks; ++b)
    {
        for (int s = 0; s < block; ++s) fg.process();
        if (fg.consumeEoc()) ++eocCount;
    }
    // Cycle period 0.1 s = 4410 samples. 12800 samples → floor(12800/4410)
    // = 2 complete cycles (each emits an EOC); the third cycle is still
    // mid-fall, so 2 is the lower bound and 3 the upper bound depending
    // on where the final block boundary lands relative to fall-end.
    REQUIRE(eocCount >= 2);
    REQUIRE(eocCount <= 3);
}

// ---------------------------------------------------------------------------
// 8. Retrigger continuity — spec test #8
// ---------------------------------------------------------------------------

TEST_CASE("FunctionGenerator: retrigger during rise stays continuous and forward",
          "[fg][retrigger]")
{
    // setRise(0.05) → 2205-sample rise → per-sample step = 1/2205 ≈ 4.5e-4.
    // Retrigger during rise should be a no-op (already going up); the
    // sample-to-sample diff at the retrigger boundary must be the same
    // 4.5e-4 step. Threshold 0.01 leaves > 20x headroom and would catch
    // any spurious "restart from 0" behaviour (diff ≈ 0.5 mid-rise).
    auto fg = makeFG(kSampleRate, 0.05f, 0.05f, 0.0f);
    fg.trigger();
    float prev = 0.0f;
    for (int i = 0; i < 500; ++i) prev = fg.process();   // mid-rise
    REQUIRE(prev > 0.05f);
    REQUIRE(prev < 0.5f);

    fg.trigger();                                        // re-trigger during rise
    float next = fg.process();
    REQUIRE(next > prev - 1e-6f);                        // forward (no reversal)
    REQUIRE_THAT(next, WithinAbs(prev, 0.01f));          // continuous
}

TEST_CASE("FunctionGenerator: retrigger during fall stays continuous and forward",
          "[fg][retrigger]")
{
    // After peak, advance ~1000 samples into a 2205-sample fall → output
    // ≈ 1 - 1000/2205 ≈ 0.547. trigger() inverts shape (identity at curve=0)
    // and switches to Rise from the same value. Next sample steps forward
    // by 1/2205 ≈ 4.5e-4. Threshold 0.01 same as above. Forward direction
    // means "not lower than previous" — would catch the buggy "reset phase
    // to 0" or "jump back to 1" implementations.
    auto fg = makeFG(kSampleRate, 0.05f, 0.05f, 0.0f);
    fg.trigger();
    const int riseSamples = static_cast<int>(0.05f * kSampleRate); // 2205
    for (int i = 0; i < riseSamples + 1000; ++i) fg.process();
    float prev = fg.currentValue();
    REQUIRE(prev > 0.4f);
    REQUIRE(prev < 0.7f);
    REQUIRE(fg.getStage() == ideath::FunctionGenerator::Stage::Fall);

    fg.trigger();
    REQUIRE(fg.getStage() == ideath::FunctionGenerator::Stage::Rise);
    float next = fg.process();
    REQUIRE(next > prev - 1e-6f);
    REQUIRE_THAT(next, WithinAbs(prev, 0.01f));
}

TEST_CASE("FunctionGenerator: retrigger during fall continuous for nonzero curve",
          "[fg][retrigger][curve]")
{
    // With curve != 0 the inverse shape must round-trip: from output v,
    // recover phase φ such that shape(φ) == v. Then the next rise sample
    // is shape(φ + 1/2205). Continuity threshold 0.02 covers worst-case
    // pow() round-trip error (~ machine eps × exponent for pow inverse,
    // < 1e-5) plus one rise step (~ 4.5e-4) plus float headroom.
    for (float curve : { -1.0f, -0.5f, 0.5f, 1.0f })
    {
        auto fg = makeFG(kSampleRate, 0.05f, 0.05f, curve);
        fg.trigger();
        const int riseSamples = static_cast<int>(0.05f * kSampleRate);
        for (int i = 0; i < riseSamples + 800; ++i) fg.process();
        float prev = fg.currentValue();
        REQUIRE(fg.getStage() == ideath::FunctionGenerator::Stage::Fall);

        fg.trigger();
        REQUIRE(fg.getStage() == ideath::FunctionGenerator::Stage::Rise);
        float next = fg.process();
        INFO("curve=" << curve << " prev=" << prev << " next=" << next);
        REQUIRE(next > prev - 1e-4f);
        REQUIRE_THAT(next, WithinAbs(prev, 0.02f));
    }
}

// ---------------------------------------------------------------------------
// 9. One-shot termination — spec test #9
// ---------------------------------------------------------------------------

TEST_CASE("FunctionGenerator: one-shot ends idle at zero and stays there",
          "[fg][oneshot]")
{
    // Run well past one full cycle (8820 samples) — 12000 puts us 3580
    // samples after fall completion. Output must remain 0 and isActive
    // must read false; the Idle branch in process() unconditionally writes
    // currentValue_ = 0.0f.
    auto fg = makeFG(kSampleRate, 0.1f, 0.1f, 0.0f);
    fg.trigger();
    for (int i = 0; i < 12000; ++i) fg.process();
    REQUIRE_FALSE(fg.isActive());
    // 1000 more samples — output must stay at exact zero.
    for (int i = 0; i < 1000; ++i)
        REQUIRE_THAT(fg.process(), WithinAbs(0.0f, 1e-7f));
}

// ---------------------------------------------------------------------------
// 10. Sample-rate independence — spec test #10
// ---------------------------------------------------------------------------

TEST_CASE("FunctionGenerator: rise time independent of sample rate", "[fg][timing]")
{
    // setRise(0.05) → peak at sample N where N = 0.05 * sr. Peak time
    // = N / sr = 0.05 s exactly (modulo the off-by-one captured in the
    // ± 1 sample tolerance of test 1). For 44100 vs 48000, both produce
    // the integer N exactly, so peak-time difference is at most
    // (1 sample / 44100) + (1 sample / 48000) ≈ 43.5 µs. Threshold 50 µs
    // leaves a thin but principled margin; widening would mask a real
    // bug like "phase increment computed in samples, not seconds".
    auto findPeakTime = [](float sr) {
        auto fg = makeFG(sr, 0.05f, 0.05f, 0.0f);
        fg.trigger();
        const int N = static_cast<int>(0.05f * sr) * 2;
        for (int i = 0; i < N; ++i)
        {
            float v = fg.process();
            if (v >= 1.0f - 1e-6f)
                return static_cast<float>(i + 1) / sr;
        }
        return -1.0f;
    };
    float t44 = findPeakTime(44100.0f);
    float t48 = findPeakTime(48000.0f);
    REQUIRE(t44 > 0.0f);
    REQUIRE(t48 > 0.0f);
    REQUIRE(std::abs(t44 - t48) < 50e-6f);
}

// ---------------------------------------------------------------------------
// Additional coverage: parameter clamping, reset, setCycle behaviour
// ---------------------------------------------------------------------------

TEST_CASE("FunctionGenerator: setCycle(true) auto-starts from idle",
          "[fg][cycle]")
{
    ideath::FunctionGenerator fg;
    fg.prepare(kSampleRate);
    fg.setRise(0.05f);
    fg.setFall(0.05f);
    REQUIRE_FALSE(fg.isActive());

    fg.setCycle(true);
    REQUIRE(fg.isActive());
    // After 100 samples we should see non-zero output (first rise sample
    // is 1/2205 ≈ 4.5e-4, so by 100 samples value > 0.04).
    float v = 0.0f;
    for (int i = 0; i < 100; ++i) v = fg.process();
    REQUIRE(v > 0.04f);
}

TEST_CASE("FunctionGenerator: setCycle(false) lets current cycle finish",
          "[fg][cycle]")
{
    // Cycle period = 2205 + 2205 = 4410 samples. Start cycling, run into
    // rise (~500 samples), then setCycle(false). The current rise + fall
    // should complete naturally (no jump to 0). Verify isActive eventually
    // goes false and output reached 1 first (no truncation).
    auto fg = makeFG(kSampleRate, 0.05f, 0.05f, 0.0f, /*cycle=*/true);
    for (int i = 0; i < 500; ++i) fg.process();
    REQUIRE(fg.getStage() == ideath::FunctionGenerator::Stage::Rise);
    fg.setCycle(false);
    // Must still complete the rise (~1700 more samples) AND the fall (~2205)
    // before going idle. Total budget: 4500 samples > remaining ~3905.
    float maxV = 0.0f;
    int idleAtSample = -1;
    for (int i = 0; i < 4500; ++i)
    {
        float v = fg.process();
        maxV = std::max(maxV, v);
        if (!fg.isActive() && idleAtSample < 0) idleAtSample = i;
    }
    REQUIRE_THAT(maxV, WithinAbs(1.0f, 1e-5f));    // peak was hit
    REQUIRE(idleAtSample > 0);                     // went idle
    REQUIRE_FALSE(fg.isActive());                  // stays idle
}

TEST_CASE("FunctionGenerator: setRise clamps extreme inputs", "[fg][clamp]")
{
    // Per spec: rise ∈ [0.001, 4.0]. Below: 0 → clamped to 0.001 (44 samples
    // at 44.1 kHz). Above: 999 → clamped to 4.0. In either case process()
    // must produce a finite, in-bounds signal.
    ideath::FunctionGenerator fg;
    fg.prepare(kSampleRate);
    fg.setRise(0.0f);
    fg.setFall(0.0f);
    fg.trigger();
    for (int i = 0; i < 1000; ++i)
    {
        float v = fg.process();
        REQUIRE(std::isfinite(v));
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 1.0f + 1e-6f);
    }

    fg.reset();
    fg.setRise(999.0f);         // → 4.0
    fg.setFall(999.0f);         // → 8.0
    fg.trigger();
    for (int i = 0; i < 1000; ++i)
    {
        float v = fg.process();
        REQUIRE(std::isfinite(v));
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 1.0f + 1e-6f);
    }
}

TEST_CASE("FunctionGenerator: setCurve clamps and stays bounded", "[fg][curve][clamp]")
{
    // setCurve(-99) → -1, setCurve(+99) → +1. Either way, output stays
    // in [0, 1] over a full cycle.
    for (float c : { -99.0f, 99.0f })
    {
        auto fg = makeFG(kSampleRate, 0.05f, 0.05f, c);
        fg.trigger();
        for (int i = 0; i < 5000; ++i)
        {
            float v = fg.process();
            REQUIRE(std::isfinite(v));
            REQUIRE(v >= 0.0f);
            REQUIRE(v <= 1.0f + 1e-6f);
        }
    }
}

TEST_CASE("FunctionGenerator: reset clears state", "[fg][reset]")
{
    auto fg = makeFG(kSampleRate, 0.1f, 0.1f, 0.0f);
    fg.trigger();
    for (int i = 0; i < 1000; ++i) fg.process();
    fg.reset();
    REQUIRE_FALSE(fg.isActive());
    // reset() assigns currentValue_ = 0.0f literal.
    REQUIRE_THAT(fg.currentValue(), WithinAbs(0.0f, 1e-7f));
    REQUIRE_FALSE(fg.consumeEoc());
}

// ---------------------------------------------------------------------------
// Long-run stability: 10 seconds in cycle mode
// ---------------------------------------------------------------------------

TEST_CASE("FunctionGenerator: 10 s of cycle mode stays bounded and EOC-coherent",
          "[fg][stability]")
{
    // Cycle period = 0.2 + 0.3 = 0.5 s → 22050 samples per cycle. 10 s
    // = 441000 samples = exactly 20 cycles (rounded by phase math: each
    // cycle ends when both segments have accumulated their integer sample
    // count, so EOC count = floor(441000 / 22050) = 20 ± 1 boundary).
    // Per-sample bounds [0, 1] follow from shape() being bounded.
    auto fg = makeFG(kSampleRate, 0.2f, 0.3f, +0.5f, /*cycle=*/true);
    int eocCount = 0;
    constexpr int N = 441000;
    for (int i = 0; i < N; ++i)
    {
        float v = fg.process();
        REQUIRE(std::isfinite(v));
        REQUIRE(v >= 0.0f - 1e-6f);
        REQUIRE(v <= 1.0f + 1e-6f);
        if (fg.consumeEoc()) ++eocCount;
    }
    REQUIRE(eocCount >= 19);
    REQUIRE(eocCount <= 21);
}
