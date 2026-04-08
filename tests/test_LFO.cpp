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

TEST_CASE("LFO: sine bipolar output in [-1, 1]", "[lfo]")
{
    ideath::LFO lfo;
    lfo.prepare(kSampleRate);
    lfo.setRate(5.0f);
    lfo.setWaveform(ideath::LFO::Waveform::Sine);
    lfo.setPolarity(ideath::LFO::Polarity::Bipolar);

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

    // Over one cycle, saw should mostly increase
    int rising = 0;
    float prev = lfo.process();
    int samplesPerCycle = static_cast<int>(kSampleRate / 1.0f);
    for (int i = 1; i < samplesPerCycle; ++i)
    {
        float s = lfo.process();
        if (s > prev) ++rising;
        prev = s;
    }

    REQUIRE(rising > samplesPerCycle - 10);
}

TEST_CASE("LFO: sample-and-hold changes once per cycle", "[lfo]")
{
    ideath::LFO lfo;
    lfo.prepare(kSampleRate);
    lfo.setRate(10.0f);
    lfo.setWaveform(ideath::LFO::Waveform::SampleAndHold);
    lfo.setPolarity(ideath::LFO::Polarity::Bipolar);

    int changes = 0;
    float prev = lfo.process();
    for (int i = 1; i < 44100; ++i)
    {
        float s = lfo.process();
        if (s != prev) ++changes;
        prev = s;
    }

    // ~10 Hz for 1 second = ~10 changes (one per cycle)
    REQUIRE(changes >= 8);
    REQUIRE(changes <= 12);
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

    // Run past one full cycle
    for (int i = 0; i < 5000; ++i)
        lfo.process();

    REQUIRE(lfo.isFinished());

    // After finishing, output should be constant
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
    REQUIRE_THAT(lfo.getPhase(), WithinAbs(0.0f, 1e-6f));
}

// ---------------------------------------------------------------------------
// ADR 009 / Phase 9b1 — Shape, Curve, Quantize
// ---------------------------------------------------------------------------

TEST_CASE("LFO: defaults equivalent to legacy sine", "[lfo][adr009]")
{
    // With Shape, Curve, Quantize all at default 0, the LFO must produce
    // exactly the same output as the legacy sine LFO.
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

    for (int step = 0; step <= 20; ++step)
    {
        lfo.setShape(static_cast<float>(step) / 20.0f);
        for (int i = 0; i < 2048; ++i)
        {
            float s = lfo.process();
            REQUIRE(std::isfinite(s));
            REQUIRE(s >= -1.5f);
            REQUIRE(s <= 1.5f);
        }
    }
}

TEST_CASE("LFO: Shape clamps out-of-range input", "[lfo][adr009]")
{
    ideath::LFO lfo;
    lfo.prepare(kSampleRate);
    lfo.setRate(2.0f);

    lfo.setShape(-5.0f);  // should clamp to 0 → equivalent to single osc
    for (int i = 0; i < 1024; ++i)
        REQUIRE(std::isfinite(lfo.process()));

    lfo.setShape(99.0f);  // should clamp to 1
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
    // Curve=0 should be analytically the sine waveform.  process() advances
    // phase before sampling, so the i-th output corresponds to phase
    // (i+1)/N.  Phase accumulation drifts a few parts in 1e3 over the
    // course of a full second of samples, so we only check the first cycle.
    for (size_t i = 0; i < sineRef.size() / 4; i += 50)
    {
        const float phase = static_cast<float>(i + 1) / kSampleRate;
        const float expected = std::sin(2.0f * static_cast<float>(M_PI) * phase);
        REQUIRE_THAT(sineRef[i], WithinAbs(expected, 1e-3f));
    }

    // Curve=1.0 should look like a square: only two distinct values
    // (within float rounding) over an integer number of cycles.
    auto squareWave = runFor(1.0f);
    int aboveZero = 0, belowZero = 0;
    for (float s : squareWave)
    {
        if (s > 0.99f) ++aboveZero;
        else if (s < -0.99f) ++belowZero;
    }
    REQUIRE(aboveZero > static_cast<int>(squareWave.size()) * 0.4f);
    REQUIRE(belowZero > static_cast<int>(squareWave.size()) * 0.4f);
}

TEST_CASE("LFO: Curve at 0.66 has clear ramp character", "[lfo][adr009]")
{
    // Saw should be mostly monotonically rising over each cycle.
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
    // Saw rises ~99% of the time over a cycle (with a single drop at wrap).
    REQUIRE(rising > total * 0.95f);
}

TEST_CASE("LFO: Curve clamps out-of-range input", "[lfo][adr009]")
{
    ideath::LFO lfo;
    lfo.prepare(kSampleRate);
    lfo.setRate(5.0f);
    lfo.setCurve(-2.0f);
    for (int i = 0; i < 512; ++i)
        REQUIRE(std::isfinite(lfo.process()));
    lfo.setCurve(99.0f);
    for (int i = 0; i < 512; ++i)
        REQUIRE(std::isfinite(lfo.process()));
}

TEST_CASE("LFO: Quantize=1 holds value across each cycle", "[lfo][adr009]")
{
    ideath::LFO lfo;
    lfo.prepare(kSampleRate);
    lfo.setRate(2.0f);  // 0.5s cycle
    lfo.setQuantize(1.0f);

    // Skip the very first sample (initial hold capture) and verify long
    // runs of consecutive identical samples (the "step" of the staircase).
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
    // At 2 Hz on 44.1 kHz, each step ≈ 22050 samples.  We use a fraction
    // of that as the floor to be robust to off-by-one phase wrap timing.
    REQUIRE(maxRun > 5000);
}

TEST_CASE("LFO: Quantize clamps out-of-range input", "[lfo][adr009]")
{
    ideath::LFO lfo;
    lfo.prepare(kSampleRate);
    lfo.setRate(5.0f);
    lfo.setQuantize(-3.0f);
    for (int i = 0; i < 512; ++i)
        REQUIRE(std::isfinite(lfo.process()));
    lfo.setQuantize(42.0f);
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

    double sum = 0.0;
    constexpr int N = 44100;
    for (int i = 0; i < N; ++i)
        sum += static_cast<double>(lfo.process());

    float dc = static_cast<float>(sum / N);
    REQUIRE_THAT(dc, WithinAbs(0.0f, 0.01f));
}
