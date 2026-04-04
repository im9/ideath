#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/TapeDelay.h>
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

TEST_CASE("TapeDelay: dry mix bypasses delay", "[tapedelay]")
{
    ideath::TapeDelay delay;
    delay.prepare(kSampleRate, 1.0f);
    delay.setDelay(0.2f);
    delay.setMix(0.0f);

    for (int i = 0; i < 100; ++i)
    {
        float input = static_cast<float>(i) / 100.0f;
        REQUIRE_THAT(delay.process(input), WithinAbs(input, 0.001f));
    }
}

TEST_CASE("TapeDelay: impulse appears around base delay", "[tapedelay]")
{
    ideath::TapeDelay delay;
    delay.prepare(kSampleRate, 1.0f);
    delay.setDelay(0.05f);
    delay.setFeedback(0.0f);
    delay.setMix(1.0f);
    delay.setWowDepth(0.0f);
    delay.setFlutterDepth(0.0f);

    const int delaySamples = static_cast<int>(0.05f * kSampleRate);
    delay.process(1.0f);

    for (int i = 1; i < delaySamples - 2; ++i)
        REQUIRE_THAT(delay.process(0.0f), WithinAbs(0.0f, 0.01f));

    bool foundImpulse = false;
    for (int i = 0; i < 5; ++i)
    {
        if (std::fabs(delay.process(0.0f)) > 0.3f)
            foundImpulse = true;
    }
    REQUIRE(foundImpulse);
}

TEST_CASE("TapeDelay: wow/flutter modulates repeats", "[tapedelay]")
{
    ideath::TapeDelay staticDelay;
    staticDelay.prepare(kSampleRate, 1.0f);
    staticDelay.setDelay(0.03f);
    staticDelay.setFeedback(0.6f);
    staticDelay.setMix(1.0f);
    staticDelay.setWowDepth(0.0f);
    staticDelay.setFlutterDepth(0.0f);

    ideath::TapeDelay modDelay;
    modDelay.prepare(kSampleRate, 1.0f);
    modDelay.setDelay(0.03f);
    modDelay.setFeedback(0.6f);
    modDelay.setMix(1.0f);
    modDelay.setWowDepth(0.003f);
    modDelay.setWowRate(0.5f);
    modDelay.setFlutterDepth(0.0008f);
    modDelay.setFlutterRate(5.0f);

    std::vector<float> a(5000), b(5000);
    for (int i = 0; i < 5000; ++i)
    {
        float input = (i == 0) ? 1.0f : 0.0f;
        a[static_cast<size_t>(i)] = staticDelay.process(input);
        b[static_cast<size_t>(i)] = modDelay.process(input);
    }

    float diff = 0.0f;
    for (int i = 0; i < 5000; ++i)
        diff += std::fabs(a[static_cast<size_t>(i)] - b[static_cast<size_t>(i)]);

    REQUIRE(diff > 0.1f);
}

TEST_CASE("TapeDelay: feedback coloring darkens repeats", "[tapedelay]")
{
    ideath::TapeDelay bright;
    bright.prepare(kSampleRate, 1.0f);
    bright.setDelay(0.02f);
    bright.setFeedback(0.8f);
    bright.setMix(1.0f);
    bright.setLowpass(18000.0f);
    bright.setHighpass(20.0f);
    bright.setWowDepth(0.0f);
    bright.setFlutterDepth(0.0f);

    ideath::TapeDelay dark;
    dark.prepare(kSampleRate, 1.0f);
    dark.setDelay(0.02f);
    dark.setFeedback(0.8f);
    dark.setMix(1.0f);
    dark.setLowpass(1500.0f);
    dark.setHighpass(200.0f);
    dark.setWowDepth(0.0f);
    dark.setFlutterDepth(0.0f);

    bright.process(1.0f);
    dark.process(1.0f);

    std::vector<float> bufBright(6000), bufDark(6000);
    for (int i = 0; i < 6000; ++i)
    {
        bufBright[static_cast<size_t>(i)] = bright.process(0.0f);
        bufDark[static_cast<size_t>(i)] = dark.process(0.0f);
    }

    REQUIRE(rms(bufDark) < rms(bufBright));
}

TEST_CASE("TapeDelay: reset clears state", "[tapedelay]")
{
    ideath::TapeDelay delay;
    delay.prepare(kSampleRate, 1.0f);
    delay.setDelay(0.1f);
    delay.setFeedback(0.8f);
    delay.setMix(1.0f);

    for (int i = 0; i < 4000; ++i)
        delay.process(0.8f);

    delay.reset();
    REQUIRE_THAT(delay.process(0.0f), WithinAbs(0.0f, 0.001f));
}

TEST_CASE("TapeDelay: output stays bounded with hot feedback", "[tapedelay]")
{
    ideath::TapeDelay delay;
    delay.prepare(kSampleRate, 1.0f);
    delay.setDelay(0.03f);
    delay.setFeedback(0.95f);
    delay.setMix(1.0f);
    delay.setDrive(4.0f);
    delay.setWowDepth(0.002f);
    delay.setFlutterDepth(0.0005f);

    for (int i = 0; i < 44100; ++i)
    {
        float input = (i < 128) ? std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / kSampleRate) : 0.0f;
        float output = delay.process(input);
        REQUIRE(std::isfinite(output));
        REQUIRE(output >= -10.0f);
        REQUIRE(output <= 10.0f);
    }
}
