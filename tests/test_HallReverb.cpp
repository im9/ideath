#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/HallReverb.h>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

TEST_CASE("HallReverb: silence in produces silence out", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSampleRate);
    rev.setMix(1.0f);

    // Zero input, zeroed buffers.  Same Freeverb structure as Reverb:
    // each comb's filterStore saturates at O(1e−25), and the 8-comb
    // sum × kWetScale(3) caps at ~1.5e−23.  Allpass chain is unity-
    // gain and adds O(kAntiDenormal) per stage.  Final output lives
    // in the 1e−23..1e−22 range — 14+ orders below the 1e−9 bound.
    for (int i = 0; i < 4410; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        REQUIRE(std::fabs(l) < 1e-9f);
        REQUIRE(std::fabs(r) < 1e-9f);
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

    // Impulse 1.0 × kInputGain = 0.015 into 8 LFO-modulated combs at
    // fb = 0.7 + 0.8·0.28 = 0.924 (size=0.8).  Same peak derivation as
    // Reverb: 8 combs × 0.015 × kWetScale(3) ≈ 0.36 at first echo.
    // LFO modulation of comb delay spreads peaks slightly in time but
    // does not reduce the peak amplitude.  Threshold 0.05 sits ~7×
    // below the predicted peak and catches a regression that silences
    // the comb bank.
    REQUIRE(maxL > 0.05f);
    REQUIRE(maxR > 0.05f);
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

TEST_CASE("HallReverb: modulation changes the tail vs unmodulated", "[hallreverb]")
{
    // The distinguishing feature of HallReverb over Reverb is LFO-
    // modulated comb delays (setModDepth controls LFO amplitude).  If
    // modulation is wired incorrectly — LFO never ticks, modDepth
    // ignored, sin LUT stuck at 0 — the tail becomes indistinguishable
    // from a plain Freeverb at the same size/damp.  Compare two
    // instances driven identically except for modDepth and verify the
    // tails diverge.
    //
    // Derivation of the divergence threshold:
    //   An LFO with modDepth = 1 modulates each comb's read position
    //   by up to ±1 sample (the library internally scales modDepth to
    //   a small offset — the exact max depth is an impl detail).  Over
    //   the tail sustain window the resulting phase-dependent reads
    //   produce a ~fraction-of-a-sample time-varying smear.  The RMS
    //   difference between modulated and unmodulated tails is at least
    //   the per-sample phase jitter × the signal RMS × a loss factor
    //   from partial cancellation — empirically in the 1e−3..1e−2
    //   range for this configuration.  Threshold > 1e−4 rejects the
    //   "modulation wired to a no-op" regression (which gives a
    //   bit-identical tail, rmsDiff = 0 up to ULP) with 10× margin.
    auto runTail = [](float modDepth) {
        ideath::HallReverb rev;
        rev.prepare(kSampleRate);
        rev.setSize(0.8f);
        rev.setDamp(0.1f);
        rev.setModDepth(modDepth);
        rev.setMix(1.0f);

        for (int i = 0; i < 4410; ++i)
            rev.process(0.5f);

        constexpr int N = 4410;
        std::vector<float> buf(N);
        for (int i = 0; i < N; ++i)
        {
            auto [l, r] = rev.process(0.0f);
            buf[static_cast<size_t>(i)] = 0.5f * (l + r);
        }
        return buf;
    };

    const auto noMod = runTail(0.0f);
    const auto withMod = runTail(1.0f);

    double sumSq = 0.0;
    for (size_t i = 0; i < noMod.size(); ++i)
    {
        const double d = static_cast<double>(withMod[i] - noMod[i]);
        sumSq += d * d;
    }
    const double rmsDiff = std::sqrt(sumSq / static_cast<double>(noMod.size()));
    INFO("mod vs no-mod tail RMS diff: " << rmsDiff);
    REQUIRE(rmsDiff > 1e-4);
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

    // mix=0: dry = 1.0, wet = 0.  Output = input · 1.0 bit-for-bit
    // (multiply by 1.0 is exact, wet branch masked by 0 multiply).
    auto [l, r] = rev.process(0.8f);
    REQUIRE_THAT(l, WithinAbs(0.8f, 1e-9f));
    REQUIRE_THAT(r, WithinAbs(0.8f, 1e-9f));
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

    // After reset, all comb/allpass/pre-delay buffers zeroed.  Same
    // derivation as Reverb reset: reads return 0, kAntiDenormal only
    // enters on write so feedback paths stay below ~1e−25 × N iterations
    // × kWetScale — far below the 1e−9 bound.
    for (int i = 0; i < 100; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        REQUIRE_THAT(l, WithinAbs(0.0f, 1e-9f));
        REQUIRE_THAT(r, WithinAbs(0.0f, 1e-9f));
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
        // ±1.5 matches the primitive's documented output ceiling (see
        // CLAUDE.md "Output levels" table — Reverb/HallReverb share the
        // same Freeverb structure).  Schroeder tuning spreads comb
        // echoes in time; LFO modulation at modDepth=1 only shifts
        // read positions by a fraction of a sample so it does not
        // increase the peak.  ~20 % headroom over the observed peak.
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
