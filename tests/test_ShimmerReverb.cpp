#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/ShimmerReverb.h>
#include <cmath>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

TEST_CASE("ShimmerReverb: silence in produces silence out", "[shimmer]")
{
    ideath::ShimmerReverb rev;
    rev.prepare(kSampleRate);
    rev.setMix(1.0f);

    for (int i = 0; i < 4410; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        REQUIRE(std::fabs(l) < 0.001f);
        REQUIRE(std::fabs(r) < 0.001f);
    }
}

TEST_CASE("ShimmerReverb: impulse produces stereo tail", "[shimmer]")
{
    ideath::ShimmerReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.7f);
    rev.setShimmer(0.5f);
    rev.setMix(1.0f);

    rev.process(1.0f);

    float maxL = 0.0f, maxR = 0.0f;
    for (int i = 0; i < 22050; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        if (std::fabs(l) > maxL) maxL = std::fabs(l);
        if (std::fabs(r) > maxR) maxR = std::fabs(r);
    }

    REQUIRE(maxL > 0.001f);
    REQUIRE(maxR > 0.001f);
}

TEST_CASE("ShimmerReverb: stereo output differs L/R", "[shimmer]")
{
    ideath::ShimmerReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.7f);
    rev.setShimmer(0.5f);
    rev.setMix(1.0f);

    rev.process(1.0f);

    bool differ = false;
    for (int i = 0; i < 22050; ++i)
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

TEST_CASE("ShimmerReverb: shimmer amount affects character", "[shimmer]")
{
    auto measureEnergy = [](float shimmerAmt) {
        ideath::ShimmerReverb rev;
        rev.prepare(kSampleRate);
        rev.setSize(0.7f);
        rev.setShimmer(shimmerAmt);
        rev.setMix(1.0f);

        // Feed a short burst
        for (int i = 0; i < 441; ++i)
            rev.process(0.5f);

        // Measure tail energy over 1 second
        float energy = 0.0f;
        for (int i = 0; i < 44100; ++i)
        {
            auto [l, r] = rev.process(0.0f);
            energy += l * l + r * r;
        }
        return energy;
    };

    float energyNoShimmer = measureEnergy(0.0f);
    float energyFullShimmer = measureEnergy(1.0f);

    // With shimmer, pitch-shifted feedback adds energy to the tail
    REQUIRE(energyFullShimmer > energyNoShimmer);
}

TEST_CASE("ShimmerReverb: size affects decay", "[shimmer]")
{
    auto measureDecay = [](float size) {
        ideath::ShimmerReverb rev;
        rev.prepare(kSampleRate);
        rev.setSize(size);
        rev.setShimmer(0.3f);
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

TEST_CASE("ShimmerReverb: freeze holds tail", "[shimmer]")
{
    ideath::ShimmerReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.7f);
    rev.setShimmer(0.3f);
    rev.setMix(1.0f);

    // Build up reverb tail
    for (int i = 0; i < 4410; ++i)
        rev.process(0.5f);

    rev.setFreeze(true);

    // Wait for 200ms crossfade to complete (shimmer → Freeverb)
    for (int i = 0; i < 8820; ++i)
        rev.process(0.0f);

    // Now measure two consecutive blocks — Freeverb freeze should sustain
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

    REQUIRE(energy1 > 0.0f);
    // After crossfade, Freeverb freeze should hold energy well
    REQUIRE(energy2 > energy1 * 0.9f);
}

TEST_CASE("ShimmerReverb: dry/wet mix", "[shimmer]")
{
    ideath::ShimmerReverb rev;
    rev.prepare(kSampleRate);
    rev.setMix(0.0f);

    auto [l, r] = rev.process(0.8f);
    REQUIRE_THAT(l, WithinAbs(0.8f, 0.01f));
    REQUIRE_THAT(r, WithinAbs(0.8f, 0.01f));
}

TEST_CASE("ShimmerReverb: output stays bounded", "[shimmer]")
{
    ideath::ShimmerReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.9f);
    rev.setShimmer(1.0f);
    rev.setMix(1.0f);

    for (int i = 0; i < 88200; ++i) // 2 seconds
    {
        float input = (i < 441) ? 1.0f : 0.0f;
        auto [l, r] = rev.process(input);
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
        REQUIRE(l >= -6.0f);
        REQUIRE(l <= 6.0f);
        REQUIRE(r >= -6.0f);
        REQUIRE(r <= 6.0f);
    }
}

TEST_CASE("ShimmerReverb: reset clears all state", "[shimmer]")
{
    ideath::ShimmerReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.8f);
    rev.setShimmer(0.5f);
    rev.setMix(1.0f);

    for (int i = 0; i < 4410; ++i)
        rev.process(0.7f);

    rev.reset();

    for (int i = 0; i < 100; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        REQUIRE_THAT(l, WithinAbs(0.0f, 0.001f));
        REQUIRE_THAT(r, WithinAbs(0.0f, 0.001f));
    }
}

TEST_CASE("ShimmerReverb: default-constructible", "[shimmer]")
{
    ideath::ShimmerReverb rev;
    auto [l, r] = rev.process(0.5f);
    REQUIRE(std::isfinite(l));
    REQUIRE(std::isfinite(r));
}

TEST_CASE("ShimmerReverb: full shimmer produces sustained ethereal tail", "[shimmer]")
{
    ideath::ShimmerReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(1.0f);
    rev.setShimmer(1.0f);
    rev.setMix(1.0f);

    // Feed a short burst (10ms)
    for (int i = 0; i < 441; ++i)
        rev.process(0.5f);

    // Skip 1 second to let the tail develop
    for (int i = 0; i < 44100; ++i)
        rev.process(0.0f);

    // Measure energy in the next second — should still be significant
    float energy = 0.0f;
    for (int i = 0; i < 44100; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        energy += l * l + r * r;
    }

    // With feedback ~0.93, the tail at 1–2 seconds should still have
    // meaningful energy (ethereal shimmer buildup)
    REQUIRE(energy > 0.01f);
}

TEST_CASE("ShimmerReverb: parameter clamping", "[shimmer]")
{
    ideath::ShimmerReverb rev;
    rev.prepare(kSampleRate);

    rev.setSize(-1.0f);
    rev.setDamp(5.0f);
    rev.setShimmer(-1.0f);
    rev.setMix(100.0f);

    for (int i = 0; i < 1000; ++i)
    {
        auto [l, r] = rev.process(0.5f);
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
    }
}

TEST_CASE("ShimmerReverb: no DC offset in output", "[shimmer]")
{
    ideath::ShimmerReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.7f);
    rev.setShimmer(0.5f);
    rev.setMix(1.0f);

    // Feed a burst
    for (int i = 0; i < 441; ++i)
        rev.process(0.5f);

    // Measure average (DC offset) over tail
    float sum = 0.0f;
    int count = 44100;
    for (int i = 0; i < count; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        sum += l + r;
    }
    float avg = sum / static_cast<float>(count * 2);

    // DC blocker should keep average near zero
    REQUIRE(std::fabs(avg) < 0.01f);
}
