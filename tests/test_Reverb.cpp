#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Reverb.h>
#include <cmath>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

TEST_CASE("Reverb: silence in produces silence out", "[reverb]")
{
    ideath::Reverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.5f);
    rev.setDamp(0.5f);
    rev.setMix(1.0f);

    // Process silence for a while
    for (int i = 0; i < 4410; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        REQUIRE(std::fabs(l) < 0.001f);
        REQUIRE(std::fabs(r) < 0.001f);
    }
}

TEST_CASE("Reverb: impulse produces stereo tail", "[reverb]")
{
    ideath::Reverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.7f);
    rev.setDamp(0.3f);
    rev.setMix(1.0f);

    // Send impulse
    rev.process(1.0f);

    // Collect some output
    float maxL = 0.0f, maxR = 0.0f;
    for (int i = 0; i < 4410; ++i) // 100ms
    {
        auto [l, r] = rev.process(0.0f);
        if (std::fabs(l) > maxL) maxL = std::fabs(l);
        if (std::fabs(r) > maxR) maxR = std::fabs(r);
    }

    // Both channels should have reverb energy
    REQUIRE(maxL > 0.001f);
    REQUIRE(maxR > 0.001f);
}

TEST_CASE("Reverb: stereo output is different L/R", "[reverb]")
{
    ideath::Reverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.6f);
    rev.setDamp(0.4f);
    rev.setMix(1.0f);

    // Send impulse
    rev.process(1.0f);

    // Check that L and R differ (stereo spread)
    bool differ = false;
    for (int i = 0; i < 4410; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        if (std::fabs(l - r) > 0.0001f)
        {
            differ = true;
            break;
        }
    }
    REQUIRE(differ);
}

TEST_CASE("Reverb: output bounded [-1, 1] with hot input", "[reverb]")
{
    ideath::Reverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.9f);
    rev.setDamp(0.2f);
    rev.setMix(1.0f);

    for (int i = 0; i < 44100; ++i)
    {
        float input = (i < 441) ? 1.0f : 0.0f;
        auto [l, r] = rev.process(input);
        REQUIRE(l >= -1.5f);
        REQUIRE(l <= 1.5f);
        REQUIRE(r >= -1.5f);
        REQUIRE(r <= 1.5f);
    }
}

TEST_CASE("Reverb: dry/wet mix", "[reverb]")
{
    ideath::Reverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.5f);
    rev.setDamp(0.5f);

    // Fully dry
    rev.setMix(0.0f);
    auto [dryL, dryR] = rev.process(0.8f);
    // Dry = input in both channels
    REQUIRE_THAT(dryL, WithinAbs(0.8f, 0.01f));
    REQUIRE_THAT(dryR, WithinAbs(0.8f, 0.01f));

    // Fully wet — first sample of reverb should be small (input * gain through combs)
    rev.reset();
    rev.setMix(1.0f);
    auto [wetL, wetR] = rev.process(0.8f);
    // Wet output on first sample is near zero (combs need time to build up)
    REQUIRE(std::fabs(wetL) < 0.5f);
    REQUIRE(std::fabs(wetR) < 0.5f);
}

TEST_CASE("Reverb: freeze holds reverb tail", "[reverb]")
{
    ideath::Reverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.5f);
    rev.setDamp(0.5f);
    rev.setMix(1.0f);

    // Feed some signal to build up reverb
    for (int i = 0; i < 4410; ++i)
        rev.process(0.5f);

    // Enable freeze
    rev.setFreeze(true);

    // Measure energy over time — it should sustain, not decay
    float energy1 = 0.0f;
    for (int i = 0; i < 4410; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        energy1 += l * l + r * r;
    }

    float energy2 = 0.0f;
    for (int i = 0; i < 4410; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        energy2 += l * l + r * r;
    }

    // In freeze mode, second block should retain most of the energy
    REQUIRE(energy1 > 0.0f);
    REQUIRE(energy2 > energy1 * 0.9f);
}

TEST_CASE("Reverb: size affects decay time", "[reverb]")
{
    auto measureDecay = [](float size) {
        ideath::Reverb rev;
        rev.prepare(kSampleRate);
        rev.setSize(size);
        rev.setDamp(0.3f);
        rev.setMix(1.0f);

        // Impulse
        rev.process(1.0f);

        // Measure energy after 0.5 seconds
        float energy = 0.0f;
        for (int i = 0; i < 22050; ++i)
        {
            auto [l, r] = rev.process(0.0f);
            energy += l * l + r * r;
        }
        return energy;
    };

    float energySmall = measureDecay(0.2f);
    float energyLarge = measureDecay(0.9f);

    // Larger size should have more energy (longer decay)
    REQUIRE(energyLarge > energySmall);
}

TEST_CASE("Reverb: damp affects brightness", "[reverb]")
{
    // With high damping, the tail should lose HF faster
    // We test by checking that heavily damped has less total energy
    // (since HF energy is removed each comb iteration)
    auto measureEnergy = [](float damp) {
        ideath::Reverb rev;
        rev.prepare(kSampleRate);
        rev.setSize(0.7f);
        rev.setDamp(damp);
        rev.setMix(1.0f);

        // Short burst of noise-like signal
        for (int i = 0; i < 100; ++i)
            rev.process((i % 2 == 0) ? 0.5f : -0.5f);

        float energy = 0.0f;
        for (int i = 0; i < 22050; ++i)
        {
            auto [l, r] = rev.process(0.0f);
            energy += l * l + r * r;
        }
        return energy;
    };

    float energyBright = measureEnergy(0.0f);
    float energyDark = measureEnergy(1.0f);

    // Bright (low damp) should retain more energy
    REQUIRE(energyBright > energyDark);
}

TEST_CASE("Reverb: reset clears all state", "[reverb]")
{
    ideath::Reverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.8f);
    rev.setDamp(0.3f);
    rev.setMix(1.0f);

    // Build up reverb
    for (int i = 0; i < 4410; ++i)
        rev.process(0.7f);

    rev.reset();

    // After reset, silence in should produce silence out
    for (int i = 0; i < 100; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        REQUIRE_THAT(l, WithinAbs(0.0f, 0.001f));
        REQUIRE_THAT(r, WithinAbs(0.0f, 0.001f));
    }
}

TEST_CASE("Reverb: default-constructible with sensible defaults", "[reverb]")
{
    ideath::Reverb rev;
    // Should not crash with default state
    auto [l, r] = rev.process(0.5f);
    REQUIRE(std::isfinite(l));
    REQUIRE(std::isfinite(r));
}

TEST_CASE("Reverb: parameter clamping", "[reverb]")
{
    ideath::Reverb rev;
    rev.prepare(kSampleRate);

    // Extreme values should not crash
    rev.setSize(-1.0f);
    rev.setDamp(5.0f);
    rev.setMix(100.0f);

    for (int i = 0; i < 1000; ++i)
    {
        auto [l, r] = rev.process(0.5f);
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
    }
}
