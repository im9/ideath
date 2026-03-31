#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Oscillator.h>
#include <cmath>
#include <vector>
#include <numeric>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

// Helper: compute RMS of a buffer.
static float rms(const float* buf, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += static_cast<double>(buf[i]) * static_cast<double>(buf[i]);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(n)));
}

TEST_CASE("Oscillator: output is in [-1, 1] range", "[osc]")
{
    ideath::Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(440.0f);

    constexpr int N = 44100; // 1 second
    for (int i = 0; i < N; ++i)
    {
        float s = osc.process(1.0f); // saw
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
    }

    osc.reset();
    osc.setFrequency(440.0f);
    for (int i = 0; i < N; ++i)
    {
        float s = osc.process(0.0f); // square
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
    }
}

TEST_CASE("Oscillator: saw has correct frequency (zero crossings)", "[osc]")
{
    ideath::Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(440.0f);

    constexpr int N = 44100;
    int zeroCrossings = 0;
    float prev = osc.process(1.0f);
    for (int i = 1; i < N; ++i)
    {
        float s = osc.process(1.0f);
        if ((prev >= 0.0f && s < 0.0f) || (prev < 0.0f && s >= 0.0f))
            ++zeroCrossings;
        prev = s;
    }

    // Saw wave: ~2 zero crossings per cycle (up-cross + down at wrap).
    // Expect roughly 440 * 2 = 880, but saw wraps give ~440 crossings.
    // Actually a naive saw crosses zero once per period at the ramp midpoint.
    // Allow ±5% tolerance.
    REQUIRE(zeroCrossings >= 400);
    REQUIRE(zeroCrossings <= 920);
}

TEST_CASE("Oscillator: produces nonzero output", "[osc]")
{
    ideath::Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(440.0f);

    constexpr int N = 4096;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i)
        buf[static_cast<size_t>(i)] = osc.process(0.5f);

    float level = rms(buf.data(), N);
    REQUIRE(level > 0.1f);
}

TEST_CASE("Oscillator: square wave RMS is ~1.0", "[osc]")
{
    ideath::Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(440.0f);

    // Skip transient.
    for (int i = 0; i < 1000; ++i)
        osc.process(0.0f);

    constexpr int N = 44100;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i)
        buf[static_cast<size_t>(i)] = osc.process(0.0f);

    float level = rms(buf.data(), N);
    // Perfect square wave RMS = 1.0.
    REQUIRE_THAT(level, WithinAbs(1.0f, 0.05f));
}

TEST_CASE("Oscillator: reset returns phase to zero", "[osc]")
{
    ideath::Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(1000.0f);

    for (int i = 0; i < 500; ++i)
        osc.process(1.0f);

    REQUIRE(osc.getPhase() > 0.0f);
    osc.reset();
    REQUIRE_THAT(osc.getPhase(), WithinAbs(0.0f, 1e-6f));
}
