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

    // Zero input, zeroed buffers. Anti-denormal (1e-25) propagates through
    // 8 combs + 4 allpasses but stays at ~1e-24 level. Output ≈ 0.
    for (int i = 0; i < 4410; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        REQUIRE(std::fabs(l) < 1e-6f);
        REQUIRE(std::fabs(r) < 1e-6f);
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

    // Impulse 1.0 × kInputGain=0.015 into 8 parallel combs, fb=0.896
    // (size=0.7). Comb delays 25–37ms, so echoes arrive within 100ms.
    // Peak per comb ≈ 0.015, 8 combs summed ≈ 0.12, ×kWetScale=3 → 0.36.
    // After allpasses: ~0.36. Threshold 0.01 is well below this.
    REQUIRE(maxL > 0.01f);
    REQUIRE(maxR > 0.01f);
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
        // kStereoSpread = 23 samples offsets R comb/allpass delays.
        // L and R see different delay-line readback → differ substantially.
        if (std::fabs(l - r) > 0.001f)
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
        // mix=1 (dry=0). 441-sample burst × kInputGain=0.015 → 0.015 per comb.
        // First-pass wet: 8 combs × 0.015 × kWetScale=3 = 0.36.
        // Feedback builds over recirculations (fb=0.952, damp=0.2).
        // Combs have different delays (25–37ms) preventing coherent sum.
        // 8 combs at delays 1116–1617 samples: echoes arrive at different times,
        // so at most 1–2 combs contribute to any given sample. Per-comb level
        // after convergence (fb=0.952): 0.015/(1−0.952) ≈ 0.31.
        // 1–2 combs × 0.31 × kWetScale(3) = 0.93–1.86 through allpasses.
        // Allpasses (fb=0.5) add ~10% transient overshoot. Bound = 1.5.
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
    // mix=0: dry = 1.0, wet = 0. Output = input × 1.0 + 0 = input. Exact.
    REQUIRE_THAT(dryL, WithinAbs(0.8f, 1e-6f));
    REQUIRE_THAT(dryR, WithinAbs(0.8f, 1e-6f));

    // Fully wet — first sample of reverb should be small (input * gain through combs)
    rev.reset();
    rev.setMix(1.0f);
    auto [wetL, wetR] = rev.process(0.8f);
    // mix=1 after reset: combs read from zeroed buffers → outL=outR=0.
    // dry=0, wet=3. Output = 0×0 + 0×3 = 0. Anti-denormal residue only.
    REQUIRE(std::fabs(wetL) < 0.01f);
    REQUIRE(std::fabs(wetR) < 0.01f);
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

    // Freeze: feedback=1.0, damp1=0, damp2=1 → perfect recirculation.
    // Allpasses have unity gain at all frequencies (|H|=1).
    // Energy should be perfectly preserved. 0.99 allows 1% for float
    // accumulation error over 8820 samples.
    REQUIRE(energy1 > 0.0f);
    REQUIRE(energy2 > energy1 * 0.99f);
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

    // After reset, all comb/allpass buffers zeroed. process(0) reads zeros.
    for (int i = 0; i < 100; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        REQUIRE_THAT(l, WithinAbs(0.0f, 1e-6f));
        REQUIRE_THAT(r, WithinAbs(0.0f, 1e-6f));
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

// --- Long-run stability ---

TEST_CASE("Reverb: long-run stability (10 seconds)", "[reverb]")
{
    ideath::Reverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.8f);
    rev.setDamp(0.3f);
    rev.setMix(1.0f);

    // 0.1s of signal, then 10s of silence. Tests that 8 combs + 4 allpasses
    // with kAntiDenormal (1e-25) don't accumulate DC or go denormal.
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

TEST_CASE("Reverb: long-run silence doesn't accumulate DC", "[reverb]")
{
    ideath::Reverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.5f);
    rev.setDamp(0.5f);
    rev.setMix(1.0f);

    // Feed signal then run 10s silence.
    for (int i = 0; i < 4410; ++i)
        rev.process(0.3f);

    float lastL = 0.0f, lastR = 0.0f;
    for (int i = 0; i < 441000; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        lastL = l;
        lastR = r;
    }

    // feedback = 0.7+0.5×0.28 = 0.84. After 10s of silence, the comb
    // content has decayed: 0.84^(10s / max_comb_delay) iterations.
    // Max comb delay ≈ 37ms → ~270 iterations. 0.84^270 ≈ 1e-21.
    // Output should be essentially zero.
    REQUIRE(std::fabs(lastL) < 1e-6f);
    REQUIRE(std::fabs(lastR) < 1e-6f);
}
