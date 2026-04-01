#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/PeakLimiter.h>
#include <cmath>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

TEST_CASE("PeakLimiter: signal below threshold passes unchanged", "[limiter]")
{
    ideath::PeakLimiter lim;
    lim.prepare(kSampleRate);
    lim.setThreshold(0.0f); // 0 dB = 1.0 linear
    lim.setLookahead(0.002f);

    // Flush lookahead delay
    for (int i = 0; i < 100; ++i)
        lim.process(0.0f);

    // Signal at 0.5 should pass through
    // Feed steady signal and check after lookahead settles
    for (int i = 0; i < 200; ++i)
        lim.process(0.5f);

    float out = lim.process(0.5f);
    REQUIRE_THAT(out, WithinAbs(0.5f, 0.05f));
}

TEST_CASE("PeakLimiter: output never exceeds threshold", "[limiter]")
{
    ideath::PeakLimiter lim;
    lim.prepare(kSampleRate);
    lim.setThreshold(-6.0f); // ~0.5 linear
    lim.setLookahead(0.003f);
    lim.setRelease(0.05f);

    float threshLin = std::pow(10.0f, -6.0f / 20.0f);

    for (int i = 0; i < 44100; ++i)
    {
        // Hot signal with peaks at 1.0
        float input = std::sin(2.0f * 3.14159f * 440.0f * i / kSampleRate);
        float out = lim.process(input);
        // After lookahead settles, output should be bounded
        if (i > 200)
            REQUIRE(std::fabs(out) <= threshLin + 0.01f);
    }
}

TEST_CASE("PeakLimiter: brickwall at 0 dB", "[limiter]")
{
    ideath::PeakLimiter lim;
    lim.prepare(kSampleRate);
    lim.setThreshold(0.0f);
    lim.setLookahead(0.005f);
    lim.setRelease(0.1f);

    for (int i = 0; i < 44100; ++i)
    {
        // Very hot signal
        float input = 5.0f * std::sin(2.0f * 3.14159f * 200.0f * i / kSampleRate);
        float out = lim.process(input);
        if (i > 250)
            REQUIRE(std::fabs(out) <= 1.01f);
    }
}

TEST_CASE("PeakLimiter: gain recovery after peak", "[limiter]")
{
    ideath::PeakLimiter lim;
    lim.prepare(kSampleRate);
    lim.setThreshold(0.0f);
    lim.setLookahead(0.002f);
    lim.setRelease(0.05f);

    // Send a loud burst
    for (int i = 0; i < 441; ++i)
        lim.process(3.0f);

    // Then quiet signal — wait long enough for release (>5× time constant)
    float lastOut = 0.0f;
    for (int i = 0; i < 22050; ++i) // 500ms
        lastOut = lim.process(0.3f);

    // After release time, gain should recover — output close to input
    REQUIRE_THAT(lastOut, WithinAbs(0.3f, 0.05f));
}

TEST_CASE("PeakLimiter: gain reduction reported correctly", "[limiter]")
{
    ideath::PeakLimiter lim;
    lim.prepare(kSampleRate);
    lim.setThreshold(0.0f);
    lim.setLookahead(0.002f);

    // No signal: no gain reduction
    for (int i = 0; i < 200; ++i)
        lim.process(0.0f);
    REQUIRE_THAT(lim.getGainReductionDb(), WithinAbs(0.0f, 0.1f));

    // Loud signal: gain reduction should be negative
    for (int i = 0; i < 441; ++i)
        lim.process(4.0f);
    REQUIRE(lim.getGainReductionDb() < -1.0f);
}

TEST_CASE("PeakLimiter: handles silence", "[limiter]")
{
    ideath::PeakLimiter lim;
    lim.prepare(kSampleRate);
    lim.setThreshold(0.0f);
    lim.setLookahead(0.002f);

    for (int i = 0; i < 4410; ++i)
    {
        float out = lim.process(0.0f);
        REQUIRE_THAT(out, WithinAbs(0.0f, 0.001f));
    }
}

TEST_CASE("PeakLimiter: reset clears state", "[limiter]")
{
    ideath::PeakLimiter lim;
    lim.prepare(kSampleRate);
    lim.setThreshold(0.0f);
    lim.setLookahead(0.002f);

    // Build up state
    for (int i = 0; i < 1000; ++i)
        lim.process(5.0f);

    lim.reset();

    // After reset, silence should produce silence
    for (int i = 0; i < 200; ++i)
    {
        float out = lim.process(0.0f);
        REQUIRE_THAT(out, WithinAbs(0.0f, 0.001f));
    }
}

TEST_CASE("PeakLimiter: default-constructible", "[limiter]")
{
    ideath::PeakLimiter lim;
    float out = lim.process(0.5f);
    REQUIRE(std::isfinite(out));
}

TEST_CASE("PeakLimiter: negative threshold works", "[limiter]")
{
    ideath::PeakLimiter lim;
    lim.prepare(kSampleRate);
    lim.setThreshold(-12.0f); // ~0.25 linear
    lim.setLookahead(0.003f);
    lim.setRelease(0.05f);

    float threshLin = std::pow(10.0f, -12.0f / 20.0f);

    for (int i = 0; i < 44100; ++i)
    {
        float input = std::sin(2.0f * 3.14159f * 440.0f * i / kSampleRate);
        float out = lim.process(input);
        if (i > 200)
            REQUIRE(std::fabs(out) <= threshLin + 0.02f);
    }
}

TEST_CASE("PeakLimiter: lookahead introduces latency", "[limiter]")
{
    ideath::PeakLimiter lim;
    lim.prepare(kSampleRate);
    lim.setThreshold(0.0f);
    lim.setLookahead(0.005f); // 5ms = 220 samples

    // Send impulse
    lim.process(0.5f);

    // First N-1 samples should be ~0 (lookahead delay)
    bool allZero = true;
    for (int i = 0; i < 200; ++i)
    {
        float out = lim.process(0.0f);
        if (std::fabs(out) > 0.01f)
            allZero = false;
    }
    REQUIRE(allZero);

    // Around sample 220, the impulse should appear
    bool foundImpulse = false;
    for (int i = 0; i < 40; ++i)
    {
        float out = lim.process(0.0f);
        if (std::fabs(out) > 0.3f)
            foundImpulse = true;
    }
    REQUIRE(foundImpulse);
}
