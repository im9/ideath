#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/BitCrusher.h>
#include <cmath>
#include <vector>
#include <set>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

TEST_CASE("BitCrusher: passthrough at full bit depth", "[bitcrusher]")
{
    ideath::BitCrusher bc;
    bc.prepare(kSampleRate);
    bc.setBitDepth(32);

    // A smooth ramp should pass through nearly unchanged
    for (int i = 0; i < 100; ++i)
    {
        float input = static_cast<float>(i) / 100.0f * 2.0f - 1.0f;
        float output = bc.process(input);
        REQUIRE_THAT(output, WithinAbs(input, 0.001f));
    }
}

TEST_CASE("BitCrusher: 1-bit reduces to two levels", "[bitcrusher]")
{
    ideath::BitCrusher bc;
    bc.prepare(kSampleRate);
    bc.setBitDepth(1);

    std::set<float> uniqueValues;
    for (int i = 0; i < 1000; ++i)
    {
        float input = static_cast<float>(i) / 500.0f - 1.0f; // ramp -1 to +1
        float output = bc.process(input);
        uniqueValues.insert(output);
        REQUIRE(output >= -1.0f);
        REQUIRE(output <= 1.0f);
    }

    // 1-bit: should produce at most 2 distinct levels
    REQUIRE(uniqueValues.size() <= 2);
}

TEST_CASE("BitCrusher: 4-bit produces limited distinct levels", "[bitcrusher]")
{
    ideath::BitCrusher bc;
    bc.prepare(kSampleRate);
    bc.setBitDepth(4);

    std::set<float> uniqueValues;
    for (int i = 0; i < 10000; ++i)
    {
        float input = static_cast<float>(i) / 5000.0f - 1.0f;
        float output = bc.process(input);
        uniqueValues.insert(output);
    }

    // 4-bit: 2^4 = 16 levels
    REQUIRE(uniqueValues.size() <= 16);
    REQUIRE(uniqueValues.size() >= 2);
}

TEST_CASE("BitCrusher: output stays in [-1, 1]", "[bitcrusher]")
{
    ideath::BitCrusher bc;
    bc.prepare(kSampleRate);
    bc.setBitDepth(4);
    bc.setDownsampleRate(8000.0f);

    for (int i = 0; i < 44100; ++i)
    {
        float input = std::sin(2.0f * 3.14159f * 440.0f * static_cast<float>(i) / kSampleRate);
        float output = bc.process(input);
        REQUIRE(output >= -1.0f);
        REQUIRE(output <= 1.0f);
    }
}

TEST_CASE("BitCrusher: downsampling creates staircase", "[bitcrusher]")
{
    ideath::BitCrusher bc;
    bc.prepare(kSampleRate);
    bc.setBitDepth(32); // no quantization, isolate downsampling effect
    bc.setDownsampleRate(4410.0f); // 1/10 of sample rate

    // Feed a ramp, expect repeated (held) values
    int holdCount = 0;
    float prev = bc.process(0.0f);
    for (int i = 1; i < 1000; ++i)
    {
        float input = static_cast<float>(i) / 1000.0f;
        float output = bc.process(input);
        if (output == prev)
            ++holdCount;
        prev = output;
    }

    // With 1/10 rate, ~90% of samples should be held
    REQUIRE(holdCount > 800);
}

TEST_CASE("BitCrusher: no downsampling when rate >= sampleRate", "[bitcrusher]")
{
    ideath::BitCrusher bc;
    bc.prepare(kSampleRate);
    bc.setBitDepth(32);
    bc.setDownsampleRate(kSampleRate); // disabled

    int changeCount = 0;
    float prev = bc.process(0.0f);
    for (int i = 1; i < 100; ++i)
    {
        float input = static_cast<float>(i) / 100.0f;
        float output = bc.process(input);
        if (output != prev)
            ++changeCount;
        prev = output;
    }

    // Every sample should be different (ramp input, no hold)
    REQUIRE(changeCount == 99);
}

TEST_CASE("BitCrusher: reset clears hold state", "[bitcrusher]")
{
    ideath::BitCrusher bc;
    bc.prepare(kSampleRate);
    bc.setBitDepth(8);
    bc.setDownsampleRate(4410.0f);

    // Process some samples
    for (int i = 0; i < 100; ++i)
        bc.process(0.5f);

    bc.reset();

    // After reset, first sample should reflect the new input
    float output = bc.process(0.0f);
    REQUIRE_THAT(output, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("BitCrusher: bit depth clamping", "[bitcrusher]")
{
    ideath::BitCrusher bc;
    bc.prepare(kSampleRate);

    // Setting invalid bit depths shouldn't crash
    bc.setBitDepth(0);   // should clamp to 1
    float out1 = bc.process(0.5f);
    REQUIRE(out1 >= -1.0f);
    REQUIRE(out1 <= 1.0f);

    bc.setBitDepth(64);  // should clamp to 32
    float out2 = bc.process(0.5f);
    REQUIRE_THAT(out2, WithinAbs(0.5f, 0.001f));
}
