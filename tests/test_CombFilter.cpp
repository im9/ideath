#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/CombFilter.h>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

static float rms(const std::vector<float>& buf)
{
    double sum = 0.0;
    for (float s : buf)
        sum += static_cast<double>(s) * static_cast<double>(s);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(buf.size())));
}

TEST_CASE("CombFilter: impulse appears at correct delay", "[comb]")
{
    ideath::CombFilter comb;
    comb.prepare(kSampleRate, 0.1f);
    comb.setDelay(0.01f);
    comb.setFeedback(0.0f);
    comb.setMix(1.0f);

    const int delaySamples = static_cast<int>(0.01f * kSampleRate);
    comb.process(1.0f);

    for (int i = 1; i < delaySamples - 1; ++i)
        REQUIRE_THAT(comb.process(0.0f), WithinAbs(0.0f, 0.01f));

    bool foundImpulse = false;
    for (int i = 0; i < 3; ++i)
    {
        if (std::fabs(comb.process(0.0f)) > 0.5f)
            foundImpulse = true;
    }
    REQUIRE(foundImpulse);
}

TEST_CASE("CombFilter: feedback produces decaying repeats", "[comb]")
{
    ideath::CombFilter comb;
    comb.prepare(kSampleRate, 0.1f);
    comb.setDelay(0.01f);
    comb.setFeedback(0.8f);
    comb.setDamp(0.2f);
    comb.setMix(1.0f);

    const int delaySamples = static_cast<int>(0.01f * kSampleRate);
    comb.process(1.0f);

    for (int i = 1; i < delaySamples; ++i)
        comb.process(0.0f);

    float echo1 = 0.0f;
    for (int i = 0; i < 3; ++i)
        echo1 = std::max(echo1, std::fabs(comb.process(0.0f)));

    for (int i = 0; i < delaySamples - 3; ++i)
        comb.process(0.0f);

    float echo2 = 0.0f;
    for (int i = 0; i < 3; ++i)
        echo2 = std::max(echo2, std::fabs(comb.process(0.0f)));

    REQUIRE(echo1 > 0.3f);
    REQUIRE(echo2 > 0.05f);
    REQUIRE(echo2 < echo1);
}

TEST_CASE("CombFilter: damp darkens the tail", "[comb]")
{
    ideath::CombFilter bright;
    bright.prepare(kSampleRate, 0.05f);
    bright.setDelay(0.005f);
    bright.setFeedback(0.95f);
    bright.setDamp(0.0f);
    bright.setMix(1.0f);

    ideath::CombFilter dark;
    dark.prepare(kSampleRate, 0.05f);
    dark.setDelay(0.005f);
    dark.setFeedback(0.95f);
    dark.setDamp(0.9f);
    dark.setMix(1.0f);

    bright.process(1.0f);
    dark.process(1.0f);

    std::vector<float> bufBright(4000), bufDark(4000);
    for (int i = 0; i < 4000; ++i)
    {
        bufBright[static_cast<size_t>(i)] = bright.process(0.0f);
        bufDark[static_cast<size_t>(i)] = dark.process(0.0f);
    }

    REQUIRE(rms(bufDark) < rms(bufBright));
}

TEST_CASE("CombFilter: reset clears state", "[comb]")
{
    ideath::CombFilter comb;
    comb.prepare(kSampleRate, 0.1f);
    comb.setDelay(0.01f);
    comb.setFeedback(0.9f);
    comb.setMix(1.0f);

    for (int i = 0; i < 1000; ++i)
        comb.process(0.8f);

    comb.reset();
    REQUIRE_THAT(comb.process(0.0f), WithinAbs(0.0f, 0.001f));
}

TEST_CASE("CombFilter: output stays bounded", "[comb]")
{
    ideath::CombFilter comb;
    comb.prepare(kSampleRate, 0.1f);
    comb.setDelay(0.002f);
    comb.setFeedback(0.99f);
    comb.setDamp(0.1f);
    comb.setMix(1.0f);

    for (int i = 0; i < 44100; ++i)
    {
        float input = (i < 32) ? 1.0f : 0.0f;
        float output = comb.process(input);
        REQUIRE(std::isfinite(output));
        REQUIRE(output >= -10.0f);
        REQUIRE(output <= 10.0f);
    }
}

TEST_CASE("CombFilter: delay is clamped to max", "[comb]")
{
    ideath::CombFilter comb;
    comb.prepare(kSampleRate, 0.05f);
    comb.setDelay(1.0f);
    REQUIRE(comb.getDelaySamples() <= 0.05f * kSampleRate + 1.0f);
}
