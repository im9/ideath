#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/ShimmerReverb.h>
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

// Goertzel single-bin power: returns |X(f)|² for `targetHz` over the
// supplied buffer.  Used to verify the shimmer feedback is actually
// pitch-shifting input energy up by an octave (vs. just smearing it).
static double goertzelPower(const float* x, int n, float targetHz, float sr)
{
    const double w = 2.0 * M_PI * static_cast<double>(targetHz) / static_cast<double>(sr);
    const double cw = std::cos(w);
    const double coeff = 2.0 * cw;
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; ++i)
    {
        s0 = static_cast<double>(x[i]) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

TEST_CASE("ShimmerReverb: feedback path produces octave-up content", "[shimmer]")
{
    // Feed a pure 220 Hz sine into the reverb.  A working shimmer should
    // build measurable energy at 440 Hz (octave up) in the wet output as
    // the pitch-shifted feedback path accumulates.  A broken pitch shifter
    // (no shifting at all) leaves wet energy concentrated at the input
    // frequency and any nearby smearing — never at the octave.
    ideath::ShimmerReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.7f);
    rev.setDamp(0.0f);
    rev.setShimmer(1.0f);
    rev.setMix(1.0f);

    constexpr int kSettle = 8192;
    constexpr int kAnalyse = 16384;

    const float fIn = 220.0f;
    const float wIn = 2.0f * static_cast<float>(M_PI) * fIn / kSampleRate;

    // Settle
    for (int i = 0; i < kSettle; ++i)
        rev.process(0.5f * std::sin(wIn * static_cast<float>(i)));

    // Capture wet output
    std::vector<float> outL(kAnalyse);
    for (int i = 0; i < kAnalyse; ++i)
    {
        auto [l, r] = rev.process(0.5f * std::sin(wIn * static_cast<float>(kSettle + i)));
        outL[i] = l;
    }

    const double powIn = goertzelPower(outL.data(), kAnalyse, 220.0f, kSampleRate);
    const double powOct = goertzelPower(outL.data(), kAnalyse, 440.0f, kSampleRate);

    INFO("input bin = " << powIn << "  octave bin = " << powOct);
    // Octave-up content must be a non-trivial fraction of the input bin.
    // With a working pitch shifter the ratio is typically > 0.05; with the
    // earlier broken implementation it was effectively zero (octave content
    // sub-1e-3 of input).
    REQUIRE(powOct > 0.0);
    REQUIRE(powOct > powIn * 0.02);
}

TEST_CASE("ShimmerReverb: silence in produces silence out", "[shimmer]")
{
    ideath::ShimmerReverb rev;
    rev.prepare(kSampleRate);
    rev.setMix(1.0f);

    // Zero input, zeroed state.  Each ModAllpass write adds
    // kAntiDenormal (1e−25) on every sample; steady-state buffer DC
    // ≈ 1e−25 / (1 − kFeedback=0.5) = 2e−25.  Cross-coupled network
    // and pitch-shifter feedback path amplify by at most kWetScale(3)
    // and a few multiplicative factors, so output stays in the
    // 1e−24..1e−23 range — 14+ orders below the 1e−9 bound.
    for (int i = 0; i < 4410; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        REQUIRE(std::fabs(l) < 1e-9f);
        REQUIRE(std::fabs(r) < 1e-9f);
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

    // Impulse through cross-coupled allpass network + pitch shift feedback.
    // Allpass chains diffuse the impulse; kWetScale=3 amplifies the wet bus.
    REQUIRE(maxL > 0.01f);
    REQUIRE(maxR > 0.01f);
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
        // Cross-coupled paths (L feeds R, R feeds L) with different allpass
        // delay lengths produce distinct L/R outputs.
        if (std::fabs(l - r) > 0.001f)
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

    // After 200ms crossfade, Freeverb freeze (fb=1.0) is fully active.
    // Energy should be well preserved. 0.95 allows for crossfade residual
    // effects (shimmer→Freeverb transition isn't perfectly energy-neutral).
    REQUIRE(energy1 > 0.0f);
    REQUIRE(energy2 > energy1 * 0.95f);
}

TEST_CASE("ShimmerReverb: dry/wet mix", "[shimmer]")
{
    ideath::ShimmerReverb rev;
    rev.prepare(kSampleRate);
    rev.setMix(0.0f);

    // mix=0: dry = 1.0, wet = 0.  Output = input · 1.0 bit-for-bit
    // (multiply by 1.0 is exact, wet branch masked by 0 multiply).
    auto [l, r] = rev.process(0.8f);
    REQUIRE_THAT(l, WithinAbs(0.8f, 1e-9f));
    REQUIRE_THAT(r, WithinAbs(0.8f, 1e-9f));
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
        // Cross-coupled allpass network + pitch-shifted feedback can build
        // higher internal levels than vanilla Freeverb. kWetScale=3 amplifies.
        // With shimmer=1.0, regenerated octave energy accumulates across
        // multiple feedback passes. ±6.0 = 2× the kWetScale × peak allpass
        // output, accounting for pitch-shift energy regeneration.
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

    // After reset, all allpass/delay/pitch-shifter buffers zeroed.
    // Same kAntiDenormal analysis as silence-in: reads return 0, the
    // 1e−25 per-sample injection is 16+ orders below the 1e−9 bound.
    for (int i = 0; i < 100; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        REQUIRE_THAT(l, WithinAbs(0.0f, 1e-9f));
        REQUIRE_THAT(r, WithinAbs(0.0f, 1e-9f));
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

    // DC blocker in the allpass network keeps average near zero.
    // Tolerance 0.05 accounts for kWetScale=3 amplifying any residual DC
    // from the pitch shifter's window function.
    REQUIRE(std::fabs(avg) < 0.05f);
}

// --- Long-run stability ---

TEST_CASE("ShimmerReverb: long-run stability (10 seconds)", "[shimmer]")
{
    ideath::ShimmerReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.8f);
    rev.setShimmer(0.7f);
    rev.setDamp(0.3f);
    rev.setMix(1.0f);

    // 0.1s signal then 10s silence. Tests cross-coupled allpass network,
    // pitch shifter window, and anti-denormal over 441k samples.
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
