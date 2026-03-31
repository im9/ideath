#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/LFO.h>
#include <cmath>
#include <set>

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
