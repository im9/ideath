#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Compressor.h>
#include <cmath>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;
static constexpr float kPi = 3.14159265f;

static float dBtoLin(float dB) { return std::pow(10.0f, dB / 20.0f); }
static float linToDb(float lin) { return 20.0f * std::log10(std::fabs(lin) + 1e-30f); }

TEST_CASE("Compressor: signal below threshold passes unchanged", "[comp]")
{
    ideath::Compressor comp;
    comp.prepare(kSampleRate);
    comp.setThreshold(-10.0f);
    comp.setRatio(4.0f);
    comp.setMakeup(0.0f);
    comp.setAttack(0.001f);
    comp.setRelease(0.05f);

    // Signal at -20 dB (below -10 threshold)
    float level = dBtoLin(-20.0f);

    // Let it settle
    for (int i = 0; i < 4410; ++i)
        comp.process(level * std::sin(kPi * 2.0f * 440.0f * i / kSampleRate));

    // Check output level matches input
    float maxOut = 0.0f;
    for (int i = 0; i < 4410; ++i)
    {
        float out = comp.process(level * std::sin(kPi * 2.0f * 440.0f * i / kSampleRate));
        if (std::fabs(out) > maxOut) maxOut = std::fabs(out);
    }

    REQUIRE_THAT(maxOut, WithinAbs(level, 0.02f));
}

TEST_CASE("Compressor: signal above threshold is reduced", "[comp]")
{
    ideath::Compressor comp;
    comp.prepare(kSampleRate);
    comp.setThreshold(-20.0f);
    comp.setRatio(4.0f);
    comp.setMakeup(0.0f);
    comp.setAttack(0.001f);
    comp.setRelease(0.05f);

    // Signal at 0 dB (well above -20 threshold)
    float level = 1.0f;

    // Let it settle
    for (int i = 0; i < 8820; ++i)
        comp.process(level * std::sin(kPi * 2.0f * 440.0f * i / kSampleRate));

    float maxOut = 0.0f;
    for (int i = 0; i < 4410; ++i)
    {
        float out = comp.process(level * std::sin(kPi * 2.0f * 440.0f * i / kSampleRate));
        if (std::fabs(out) > maxOut) maxOut = std::fabs(out);
    }

    // Output should be significantly reduced
    REQUIRE(maxOut < level * 0.8f);
    REQUIRE(maxOut > 0.0f);
}

TEST_CASE("Compressor: higher ratio means more compression", "[comp]")
{
    auto measurePeak = [](float ratio) {
        ideath::Compressor comp;
        comp.prepare(kSampleRate);
        comp.setThreshold(-20.0f);
        comp.setRatio(ratio);
        comp.setMakeup(0.0f);
        comp.setAttack(0.001f);
        comp.setRelease(0.05f);

        for (int i = 0; i < 8820; ++i)
            comp.process(std::sin(kPi * 2.0f * 440.0f * i / kSampleRate));

        float maxOut = 0.0f;
        for (int i = 0; i < 4410; ++i)
        {
            float out = comp.process(std::sin(kPi * 2.0f * 440.0f * i / kSampleRate));
            if (std::fabs(out) > maxOut) maxOut = std::fabs(out);
        }
        return maxOut;
    };

    float peakLow = measurePeak(2.0f);
    float peakHigh = measurePeak(20.0f);

    // Higher ratio should produce lower output
    REQUIRE(peakHigh < peakLow);
}

TEST_CASE("Compressor: makeup gain boosts output", "[comp]")
{
    ideath::Compressor comp;
    comp.prepare(kSampleRate);
    comp.setThreshold(-20.0f);
    comp.setRatio(4.0f);
    comp.setAttack(0.001f);
    comp.setRelease(0.05f);

    // Without makeup
    comp.setMakeup(0.0f);
    for (int i = 0; i < 8820; ++i)
        comp.process(std::sin(kPi * 2.0f * 440.0f * i / kSampleRate));
    float maxNoMakeup = 0.0f;
    for (int i = 0; i < 4410; ++i)
    {
        float out = comp.process(std::sin(kPi * 2.0f * 440.0f * i / kSampleRate));
        if (std::fabs(out) > maxNoMakeup) maxNoMakeup = std::fabs(out);
    }

    // With 6dB makeup
    comp.reset();
    comp.setMakeup(6.0f);
    for (int i = 0; i < 8820; ++i)
        comp.process(std::sin(kPi * 2.0f * 440.0f * i / kSampleRate));
    float maxWithMakeup = 0.0f;
    for (int i = 0; i < 4410; ++i)
    {
        float out = comp.process(std::sin(kPi * 2.0f * 440.0f * i / kSampleRate));
        if (std::fabs(out) > maxWithMakeup) maxWithMakeup = std::fabs(out);
    }

    REQUIRE(maxWithMakeup > maxNoMakeup * 1.5f);
}

TEST_CASE("Compressor: gain reduction reported correctly", "[comp]")
{
    ideath::Compressor comp;
    comp.prepare(kSampleRate);
    comp.setThreshold(-20.0f);
    comp.setRatio(4.0f);
    comp.setAttack(0.001f);
    comp.setRelease(0.05f);

    // No signal: no gain reduction
    for (int i = 0; i < 200; ++i)
        comp.process(0.0f);
    REQUIRE_THAT(comp.getGainReductionDb(), WithinAbs(0.0f, 0.1f));

    // Loud signal: gain reduction should be negative
    for (int i = 0; i < 4410; ++i)
        comp.process(std::sin(kPi * 2.0f * 440.0f * i / kSampleRate));
    REQUIRE(comp.getGainReductionDb() < -1.0f);
}

TEST_CASE("Compressor: ratio 1.0 means no compression", "[comp]")
{
    ideath::Compressor comp;
    comp.prepare(kSampleRate);
    comp.setThreshold(-20.0f);
    comp.setRatio(1.0f);
    comp.setMakeup(0.0f);
    comp.setAttack(0.001f);
    comp.setRelease(0.05f);

    float level = 0.8f;
    for (int i = 0; i < 8820; ++i)
        comp.process(level * std::sin(kPi * 2.0f * 440.0f * i / kSampleRate));

    float maxOut = 0.0f;
    for (int i = 0; i < 4410; ++i)
    {
        float out = comp.process(level * std::sin(kPi * 2.0f * 440.0f * i / kSampleRate));
        if (std::fabs(out) > maxOut) maxOut = std::fabs(out);
    }

    // Should be essentially unchanged
    REQUIRE_THAT(maxOut, WithinAbs(level, 0.05f));
}

TEST_CASE("Compressor: reset clears state", "[comp]")
{
    ideath::Compressor comp;
    comp.prepare(kSampleRate);
    comp.setThreshold(-20.0f);
    comp.setRatio(4.0f);
    comp.setAttack(0.001f);
    comp.setRelease(0.05f);

    // Build up compression state
    for (int i = 0; i < 4410; ++i)
        comp.process(1.0f);

    comp.reset();

    // After reset, no gain reduction
    REQUIRE_THAT(comp.getGainReductionDb(), WithinAbs(0.0f, 0.1f));

    // Silence in, silence out
    float out = comp.process(0.0f);
    REQUIRE_THAT(out, WithinAbs(0.0f, 0.001f));
}

TEST_CASE("Compressor: default-constructible", "[comp]")
{
    ideath::Compressor comp;
    float out = comp.process(0.5f);
    REQUIRE(std::isfinite(out));
}

TEST_CASE("Compressor: handles silence", "[comp]")
{
    ideath::Compressor comp;
    comp.prepare(kSampleRate);
    comp.setThreshold(-20.0f);
    comp.setRatio(4.0f);

    for (int i = 0; i < 4410; ++i)
    {
        float out = comp.process(0.0f);
        REQUIRE_THAT(out, WithinAbs(0.0f, 0.001f));
    }
}
