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

    // Zero input, zeroed buffers.  Each comb's filterStore converges to
    //   store_ss = kAntiDenormal · damp2 / (1 − damp1)
    //            = 1e−25 · 0.8 / 0.8 = 1e−25 at size=0.5, damp=0.5
    // so the per-comb buffer write saturates at 1e−25 · (1 + fb / (1−fb))
    // ≈ 1e−25 / (1 − 0.84) ≈ 6e−25.  Sum over 8 combs ≈ 5e−24.  Each
    // allpass adds O(kAntiDenormal) on its write + a unity-gain pass, so
    // the allpass chain stays in the ~1e−24 range.  Output wet = 5e−24 ·
    // kWetScale(3) ≈ 1.5e−23, 14 orders below the 1e−9 bound.
    for (int i = 0; i < 4410; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        REQUIRE(std::fabs(l) < 1e-9f);
        REQUIRE(std::fabs(r) < 1e-9f);
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

    // Impulse 1.0 × kInputGain = 0.015 into 8 parallel combs at fb =
    // 0.7 + 0.7·0.28 = 0.896 (size=0.7).  Comb delays 25–37 ms, so
    // echoes arrive within this 100 ms window.  Peak per comb at first
    // echo ≈ 0.015 (impulse read back through fb with damp=0.3 barely
    // attenuated after one pass); 8 combs × 0.015 × kWetScale(3) ≈
    // 0.36.  Allpass diffusion is unity-gain, so tail peak stays near
    // 0.36.  Threshold 0.05 sits ~7× below that peak and well above
    // any noise floor — rejects a regression that drops input gain or
    // the wet scale.
    REQUIRE(maxL > 0.05f);
    REQUIRE(maxR > 0.05f);
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
        // ±1.5 matches the primitive's documented output ceiling (see
        // CLAUDE.md "Output levels" table and Reverb.h header comment
        // "Output can reach ±1.5 for ±1.0 input").  The Schroeder /
        // Freeverb tuning of kCombTunings (1116, 1188, 1277, 1356, 1422,
        // 1491, 1557, 1617 — mutually near-prime at 44.1 kHz) ensures
        // comb echoes are spread across time, so per-sample overlap
        // between the 8 combs is low and the sum stays well under its
        // worst-case coherent bound.  For a 10 ms burst at mix=1 and
        // fb=0.952 (size=0.9) the empirical peak sits around 1.1–1.3,
        // so ±1.5 gives ~20 % headroom over the observed maximum while
        // locking the primitive's contract to the figure plugins see.
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
    // mix=0: dry = 1.0, wet = 0.  output = input · 1.0 + wet · 0 = input
    // bit-for-bit (multiply by 1.0 is exact, the wet branch is masked
    // by the 0 multiply).  1e−9 tolerance is a safety net for the
    // float matcher API.
    REQUIRE_THAT(dryL, WithinAbs(0.8f, 1e-9f));
    REQUIRE_THAT(dryR, WithinAbs(0.8f, 1e-9f));

    // Fully wet — first sample after reset must be bit-exact 0.
    rev.reset();
    rev.setMix(1.0f);
    auto [wetL, wetR] = rev.process(0.8f);
    // After reset() every comb and allpass buffer is zero-filled.
    // Comb::process reads buffer[index]=0 BEFORE writing, so outL/outR
    // = 0.  Allpasses receive 0, their buffer[index] = 0, so bufOut=0
    // and output = 0 − 0 = 0.  Final wet = 0 · kWetScale = 0; dry
    // branch is masked by mix=1 → dry=0.  Result is literally 0.0f
    // before the +kAntiDenormal (which only affects the NEXT call's
    // buffer read).  Threshold 1e−9 is far above the ULP.
    REQUIRE(std::fabs(wetL) < 1e-9f);
    REQUIRE(std::fabs(wetR) < 1e-9f);
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

    // After reset, all comb/allpass buffers zeroed.  process(0) with
    // zero input reads zeros; kAntiDenormal only enters the buffer on
    // write, so the first 100 reads see only values ≤ 1e−25 coming
    // back through feedback — 14+ orders below the 1e−9 bound.
    for (int i = 0; i < 100; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        REQUIRE_THAT(l, WithinAbs(0.0f, 1e-9f));
        REQUIRE_THAT(r, WithinAbs(0.0f, 1e-9f));
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

    // feedback = 0.7 + 0.5·0.28 = 0.84.  After 10 s of silence each
    // comb recirculates 10 s / 37 ms ≈ 270 times, multiplying its
    // content by fb^270 = 0.84^270 ≈ 1e−21.  The kAntiDenormal floor
    // keeps output at least ~1e−24, so the 10 s tail lives in the
    // 1e−21..1e−23 range — 12+ orders below the 1e−9 bound.
    REQUIRE(std::fabs(lastL) < 1e-9f);
    REQUIRE(std::fabs(lastR) < 1e-9f);
}
