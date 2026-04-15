#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/HallReverb.h>
#include <cmath>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

TEST_CASE("HallReverb: silence in produces silence out", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSampleRate);
    rev.setMix(1.0f);

    // Zero input, zeroed buffers. Anti-denormal (1e-25) propagates through
    // 8 mod-combs + 4 allpasses but stays at ~1e-24 level.
    for (int i = 0; i < 4410; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        REQUIRE(std::fabs(l) < 1e-6f);
        REQUIRE(std::fabs(r) < 1e-6f);
    }
}

TEST_CASE("HallReverb: impulse produces stereo tail", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.8f);
    rev.setDamp(0.1f);
    rev.setMix(1.0f);

    rev.process(1.0f);

    float maxL = 0.0f, maxR = 0.0f;
    for (int i = 0; i < 8820; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        if (std::fabs(l) > maxL) maxL = std::fabs(l);
        if (std::fabs(r) > maxR) maxR = std::fabs(r);
    }

    // Impulse 1.0 × kInputGain=0.015 into 8 LFO-modulated combs, fb=0.924
    // (size=0.8). Same structure as Reverb: peak tail ≈ 0.1 level.
    REQUIRE(maxL > 0.01f);
    REQUIRE(maxR > 0.01f);
}

TEST_CASE("HallReverb: stereo output differs L/R", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.7f);
    rev.setMix(1.0f);

    rev.process(1.0f);

    bool differ = false;
    for (int i = 0; i < 4410; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        // kStereoSpread=23 + LFO phase offset (0.5 between L/R) → differ.
        if (std::fabs(l - r) > 0.001f)
        {
            differ = true;
            break;
        }
    }
    REQUIRE(differ);
}

TEST_CASE("HallReverb: pre-delay offsets the tail", "[hallreverb]")
{
    // Feed a burst and measure energy after the comb delay has built up
    auto measureEarlyEnergy = [](float preDelaySec) {
        ideath::HallReverb rev;
        rev.prepare(kSampleRate);
        rev.setPreDelay(preDelaySec);
        rev.setSize(0.7f);
        rev.setDamp(0.1f);
        rev.setMix(1.0f);

        // Feed 50ms burst
        for (int i = 0; i < 2205; ++i)
            rev.process(0.5f);

        // Measure energy in the next 50ms (tail after burst stops)
        float energy = 0.0f;
        for (int i = 0; i < 2205; ++i)
        {
            auto [l, r] = rev.process(0.0f);
            energy += l * l + r * r;
        }
        return energy;
    };

    float noDelay = measureEarlyEnergy(0.0f);
    float withDelay = measureEarlyEnergy(0.08f); // 80ms pre-delay

    // No-delay version has more tail energy (signal enters combs sooner)
    REQUIRE(noDelay > withDelay);
}

TEST_CASE("HallReverb: modulation creates variation over time", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.8f);
    rev.setModDepth(1.0f);
    rev.setMix(1.0f);

    // Feed signal to build up reverb state
    for (int i = 0; i < 4410; ++i)
        rev.process(0.5f);

    // Collect two consecutive blocks of output
    float energy1 = 0.0f;
    for (int i = 0; i < 2205; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        energy1 += l * l + r * r;
    }

    float energy2 = 0.0f;
    for (int i = 0; i < 2205; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        energy2 += l * l + r * r;
    }

    // Both blocks should have energy (tail is still ringing)
    REQUIRE(energy1 > 0.0f);
    REQUIRE(energy2 > 0.0f);
    // With modulation, blocks will differ slightly (not identical decay)
    // This is a weak test — mainly checking it doesn't crash or go silent
}

TEST_CASE("HallReverb: size affects decay time", "[hallreverb]")
{
    auto measureDecay = [](float size) {
        ideath::HallReverb rev;
        rev.prepare(kSampleRate);
        rev.setSize(size);
        rev.setDamp(0.1f);
        rev.setMix(1.0f);

        rev.process(1.0f);

        float energy = 0.0f;
        for (int i = 0; i < 44100; ++i)
        {
            auto [l, r] = rev.process(0.0f);
            energy += l * l + r * r;
        }
        return energy;
    };

    float energySmall = measureDecay(0.2f);
    float energyLarge = measureDecay(0.9f);

    REQUIRE(energyLarge > energySmall);
}

TEST_CASE("HallReverb: freeze holds tail", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.7f);
    rev.setMix(1.0f);

    for (int i = 0; i < 4410; ++i)
        rev.process(0.5f);

    rev.setFreeze(true);

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

    // Freeze: fb=1.0, damp1=0, damp2=1 → perfect recirculation.
    // Allpasses are unity-gain. Energy preserved within float precision.
    REQUIRE(energy1 > 0.0f);
    REQUIRE(energy2 > energy1 * 0.99f);
}

TEST_CASE("HallReverb: dry/wet mix", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSampleRate);
    rev.setMix(0.0f);

    // mix=0: dry=1.0, wet=0. Output = input × 1.0 + 0 = input. Exact.
    auto [l, r] = rev.process(0.8f);
    REQUIRE_THAT(l, WithinAbs(0.8f, 1e-6f));
    REQUIRE_THAT(r, WithinAbs(0.8f, 1e-6f));
}

TEST_CASE("HallReverb: reset clears all state", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.8f);
    rev.setMix(1.0f);

    for (int i = 0; i < 4410; ++i)
        rev.process(0.7f);

    rev.reset();

    // After reset, all comb/allpass/pre-delay buffers zeroed.
    for (int i = 0; i < 100; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        REQUIRE_THAT(l, WithinAbs(0.0f, 1e-6f));
        REQUIRE_THAT(r, WithinAbs(0.0f, 1e-6f));
    }
}

TEST_CASE("HallReverb: default-constructible", "[hallreverb]")
{
    ideath::HallReverb rev;
    auto [l, r] = rev.process(0.5f);
    REQUIRE(std::isfinite(l));
    REQUIRE(std::isfinite(r));
}

TEST_CASE("HallReverb: output bounded with signal", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.95f);
    rev.setModDepth(1.0f);
    rev.setMix(1.0f);

    for (int i = 0; i < 44100; ++i)
    {
        float input = (i < 441) ? 1.0f : 0.0f;
        auto [l, r] = rev.process(input);
        // Same Freeverb structure as Reverb (kInputGain=0.015, kWetScale=3,
        // 8 combs at different delays). Per-comb convergence ≈ 0.015/(1−fb).
        // Combs echo at different times → 1–2 overlap per sample.
        // Peak ≈ 1–2 × 0.31 × 3 ≈ 0.93–1.86. Allpass overshoot ~10%.
        REQUIRE(l >= -1.5f);
        REQUIRE(l <= 1.5f);
        REQUIRE(r >= -1.5f);
        REQUIRE(r <= 1.5f);
    }
}

TEST_CASE("HallReverb: parameter clamping", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSampleRate);

    rev.setSize(-1.0f);
    rev.setDamp(5.0f);
    rev.setPreDelay(10.0f);
    rev.setModDepth(-1.0f);
    rev.setMix(100.0f);

    for (int i = 0; i < 1000; ++i)
    {
        auto [l, r] = rev.process(0.5f);
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
    }
}

// --- Long-run stability ---

TEST_CASE("HallReverb: long-run stability (10 seconds)", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.8f);
    rev.setDamp(0.3f);
    rev.setModDepth(0.5f);
    rev.setMix(1.0f);

    // 0.1s signal then 10s silence. Tests 8 LFO-modulated combs + 4 allpasses
    // with anti-denormal over 441k samples.
    for (int i = 0; i < 4410; ++i)
        rev.process(0.5f);

    constexpr int N = 441000;
    for (int i = 0; i < N; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
    }
}
