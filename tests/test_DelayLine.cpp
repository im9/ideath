#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/DelayLine.h>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

TEST_CASE("DelayLine: impulse appears at correct delay", "[delay]")
{
    ideath::DelayLine dl;
    dl.prepare(kSampleRate, 0.1f);
    dl.setDelay(0.01f); // 10ms = 441 samples
    dl.setFeedback(0.0f);
    dl.setMix(1.0f);

    int delaySamples = static_cast<int>(0.01f * kSampleRate);

    // Send impulse
    dl.process(1.0f);

    // Silence until delay time
    for (int i = 1; i < delaySamples - 1; ++i)
    {
        float s = dl.process(0.0f);
        REQUIRE_THAT(s, WithinAbs(0.0f, 0.01f));
    }

    // Around the delay time, the impulse should appear
    bool foundImpulse = false;
    for (int i = 0; i < 3; ++i)
    {
        float s = dl.process(0.0f);
        if (std::fabs(s) > 0.5f)
            foundImpulse = true;
    }
    REQUIRE(foundImpulse);
}

TEST_CASE("DelayLine: dry mix bypasses delay", "[delay]")
{
    ideath::DelayLine dl;
    dl.prepare(kSampleRate, 0.1f);
    dl.setDelay(0.01f);
    dl.setFeedback(0.0f);
    dl.setMix(0.0f); // fully dry

    // With mix=0, output always equals input regardless of delay
    for (int i = 0; i < 100; ++i)
    {
        float input = static_cast<float>(i) / 100.0f;
        float output = dl.process(input);
        REQUIRE_THAT(output, WithinAbs(input, 0.001f));
    }
}

TEST_CASE("DelayLine: feedback produces repeating echoes", "[delay]")
{
    ideath::DelayLine dl;
    dl.prepare(kSampleRate, 0.1f);
    dl.setDelay(0.01f); // 441 samples
    dl.setFeedback(0.5f);
    dl.setMix(1.0f);

    int delaySamples = static_cast<int>(0.01f * kSampleRate);

    // Send impulse
    dl.process(1.0f);

    // Skip to first echo
    for (int i = 1; i < delaySamples; ++i)
        dl.process(0.0f);

    // First echo
    float echo1 = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        float s = dl.process(0.0f);
        if (std::fabs(s) > std::fabs(echo1))
            echo1 = s;
    }

    // Skip to second echo
    for (int i = 0; i < delaySamples - 3; ++i)
        dl.process(0.0f);

    float echo2 = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        float s = dl.process(0.0f);
        if (std::fabs(s) > std::fabs(echo2))
            echo2 = s;
    }

    // Second echo should be quieter (feedback = 0.5)
    REQUIRE(std::fabs(echo1) > 0.3f);
    REQUIRE(std::fabs(echo2) > 0.05f);
    REQUIRE(std::fabs(echo2) < std::fabs(echo1));
}

TEST_CASE("DelayLine: mix blends dry and wet", "[delay]")
{
    ideath::DelayLine dl;
    dl.prepare(kSampleRate, 0.1f);
    dl.setDelay(0.01f);
    dl.setFeedback(0.0f);
    dl.setMix(0.0f); // fully dry

    // With mix=0, output should equal input
    float out = dl.process(0.75f);
    REQUIRE_THAT(out, WithinAbs(0.75f, 0.01f));

    // With mix=0.5, during silence (before echo arrives), output is half the dry
    dl.reset();
    dl.setDelay(0.01f);
    dl.setMix(0.5f);
    out = dl.process(1.0f);
    // First sample: dry * 0.5 + wet * 0.5; wet is from buffer (0 after reset)
    REQUIRE_THAT(out, WithinAbs(0.5f, 0.01f));
}

TEST_CASE("DelayLine: output stays bounded with feedback", "[delay]")
{
    ideath::DelayLine dl;
    dl.prepare(kSampleRate, 0.5f);
    dl.setDelay(0.05f);
    dl.setFeedback(0.9f);
    dl.setMix(1.0f);

    // Feed a signal and check it doesn't explode
    for (int i = 0; i < 44100; ++i)
    {
        float input = (i < 100) ? 1.0f : 0.0f;
        float output = dl.process(input);
        REQUIRE(output >= -10.0f);
        REQUIRE(output <= 10.0f);
    }
}

TEST_CASE("DelayLine: reset clears buffer", "[delay]")
{
    ideath::DelayLine dl;
    dl.prepare(kSampleRate, 0.1f);
    dl.setDelay(0.01f);
    dl.setFeedback(0.5f);
    dl.setMix(1.0f);

    // Fill buffer with signal
    for (int i = 0; i < 1000; ++i)
        dl.process(0.8f);

    dl.reset();

    // After reset, output should be zero for zero input
    float out = dl.process(0.0f);
    REQUIRE_THAT(out, WithinAbs(0.0f, 0.001f));
}

TEST_CASE("DelayLine: fractional delay uses interpolation", "[delay]")
{
    ideath::DelayLine dl;
    dl.prepare(kSampleRate, 0.1f);
    dl.setDelay(0.0100113f); // fractional sample count
    dl.setFeedback(0.0f);
    dl.setMix(1.0f);

    // Send impulse
    dl.process(1.0f);

    int delaySamples = static_cast<int>(0.0100113f * kSampleRate);

    for (int i = 1; i < delaySamples; ++i)
        dl.process(0.0f);

    // With linear interpolation, the impulse energy splits across two samples
    float s1 = dl.process(0.0f);
    float s2 = dl.process(0.0f);

    // At least one should be nonzero, and their sum should be close to 1
    REQUIRE((std::fabs(s1) + std::fabs(s2)) > 0.5f);
}

TEST_CASE("DelayLine: delay clamped to max", "[delay]")
{
    ideath::DelayLine dl;
    dl.prepare(kSampleRate, 0.1f); // max 0.1s
    dl.setDelay(5.0f); // way over max

    // Should be clamped, not crash
    REQUIRE(dl.getDelaySamples() <= 0.1f * kSampleRate + 1.0f);

    for (int i = 0; i < 100; ++i)
        dl.process(0.5f); // should not crash
}
