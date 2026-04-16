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

TEST_CASE("ShimmerReverb: freeze crossfade preserves RMS (no midpoint dip)", "[shimmer]")
{
    // The freeze transition crossfades between two decorrelated signals:
    // the shimmer path and the Freeverb freeze path (fed from shimmer).
    // A linear blend `a·S + (1−a)·F` with a going 0→1 drops ~3 dB at the
    // midpoint for decorrelated S,F of equal amplitude:
    //   RMS_mid = √(½·S² + ½·F²) = S·√½ ≈ 0.707·S  when S=F, uncorrelated.
    // Equal-power (sin/cos) crossfade satisfies a² + b² = 1 so RMS stays
    // ~constant. We probe the midpoint of the 200 ms crossfade under a
    // steady broadband stimulus and require the midpoint RMS to stay within
    // 15 % of the minimum endpoint RMS — clearly separating 0.707 (linear,
    // fails) from ≈1.0 (equal-power, passes). The 15 % margin covers
    // partial correlation between S and F (both share input history).
    ideath::ShimmerReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.7f);
    rev.setShimmer(0.5f);
    rev.setDamp(0.3f);
    rev.setMix(1.0f);

    // Broadband stimulus: three incommensurate high-frequency sines so a
    // 10 ms RMS window still spans ≥10 periods of each component (997 Hz
    // → 1 ms period). Avoids the window-integration error that a 220 Hz
    // component would cause over a narrow window.
    auto stim = [](int i) {
        float t = static_cast<float>(i) / kSampleRate;
        return (std::sin(2.0f * static_cast<float>(M_PI) * 997.0f  * t)
              + std::sin(2.0f * static_cast<float>(M_PI) * 1447.0f * t)
              + std::sin(2.0f * static_cast<float>(M_PI) * 2011.0f * t))
             * (0.3f / 3.0f);
    };

    // Drive for 1 s to reach shimmer steady state. feedback_ ≈ 0.60 at
    // the chosen settings; 1 s is multiple RT60 settling constants for
    // the tuned delay network, so rmsPre is a stable estimate of |S|.
    int tSample = 0;
    for (int i = 0; i < 44100; ++i)
        rev.process(stim(tSample++));

    // Engage freeze. Keep stimulus running so |S| (shimmer RMS) stays at
    // its steady-state value during the measurement; silencing the input
    // would conflate the blend's tonal shift with natural amplitude decay.
    rev.setFreeze(true);

    // Measurement windows: 10 ms = 441 samples. At 997 Hz that is ~10
    // full cycles, sufficient for a stable RMS estimate while keeping the
    // window narrow enough that xfade varies by only ±0.025 across it —
    // endpoint samples (xfade ≈ 0 and ≈ 1) and midpoint samples
    // (xfade ≈ 0.5) are each read at the value they are supposed to probe.
    constexpr int kWin = 441; // 10 ms
    constexpr int kXfadeLen = 8820; // 200 ms at 44.1 kHz, matches impl
    constexpr int kMid = kXfadeLen / 2; // 4410

    auto rmsWindow = [&]() {
        float sum = 0.0f;
        for (int i = 0; i < kWin; ++i)
        {
            auto [l, r] = rev.process(stim(tSample++));
            sum += l * l + r * r;
        }
        return std::sqrt(sum / (2.0f * kWin));
    };

    // Pre window: samples 0..kWin-1 after setFreeze, xfade ∈ [0, 0.05).
    // Output ≈ shim·cos(small) ≈ shim — gives |S|.
    const float rmsPre = rmsWindow();

    // Advance to mid window centred on xfade = 0.5.
    for (int i = 0; i < (kMid - kWin / 2) - kWin; ++i)
        rev.process(stim(tSample++));
    const float rmsMid = rmsWindow();

    // Advance so the final window ends exactly at xfade = 1 (kXfadeLen
    // samples from setFreeze). Measures Freeverb at the same moment the
    // crossfade completes — before any further freeze-with-input
    // accumulation changes |F|.
    const int samplesConsumed = 2 * kWin + (kMid - kWin / 2) - kWin;
    for (int i = 0; i < kXfadeLen - samplesConsumed - kWin; ++i)
        rev.process(stim(tSample++));
    const float rmsPost = rmsWindow();

    INFO("rmsPre=" << rmsPre << " rmsMid=" << rmsMid << " rmsPost=" << rmsPost);

    // Equal-power crossfade of decorrelated S,F gives
    //   RMS_mid = √(sin²(π/4)·S² + cos²(π/4)·F²) = √((S² + F²)/2)
    // i.e. the quadratic mean of the endpoints.
    // Linear crossfade gives ½·√(S² + F²) = 0.707× the quadratic mean.
    // The ratio equal-power / linear at midpoint is √2, independent of
    // |S| and |F|. Threshold 0.85 cleanly separates the two regimes; the
    // 15 % margin absorbs partial correlation between S and F (both share
    // input history through the shimmer → Freeverb feed).
    const float refQMean = std::sqrt((rmsPre * rmsPre + rmsPost * rmsPost) * 0.5f);
    REQUIRE(rmsMid > refQMean * 0.85f);
}

TEST_CASE("ShimmerReverb: freeze + continuous input does not accumulate", "[shimmer]")
{
    // When freeze is engaged, the internal Freverb's comb feedback is
    // raised to 1.0 to hold the captured tail. If that Freverb continues
    // to receive fresh input after freeze, each sample is added to a
    // buffer that now never decays — amplitude grows linearly, and
    // "freeze" becomes "accumulate until clipping". The correct
    // behaviour is to capture the tail at the moment of freeze and hold
    // it, irrespective of whether the host keeps driving the reverb.
    ideath::ShimmerReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.7f);
    rev.setShimmer(0.5f);
    rev.setDamp(0.3f);
    rev.setMix(1.0f);

    auto stim = [](int i) {
        float t = static_cast<float>(i) / kSampleRate;
        return (std::sin(2.0f * static_cast<float>(M_PI) * 997.0f  * t)
              + std::sin(2.0f * static_cast<float>(M_PI) * 1447.0f * t)
              + std::sin(2.0f * static_cast<float>(M_PI) * 2011.0f * t))
             * (0.3f / 3.0f);
    };

    // Build shimmer steady state (1 s ≫ RT60 of the tuned delay network).
    int t = 0;
    for (int i = 0; i < 44100; ++i)
        rev.process(stim(t++));

    rev.setFreeze(true);

    // Advance past the 200 ms crossfade plus 50 ms settle so we measure
    // purely the frozen-Freverb output path (xfade saturated at 1.0 →
    // shimGain = cos(π/2) = 0, so only Freverb contributes to output).
    for (int i = 0; i < 8820 + 2205; ++i)
        rev.process(stim(t++));

    auto measureRms = [&](int n) {
        float s = 0.0f;
        for (int k = 0; k < n; ++k)
        {
            auto [l, r] = rev.process(stim(t++));
            s += l * l + r * r;
        }
        return std::sqrt(s / (2.0f * n));
    };

    constexpr int kWin = 4410; // 100 ms
    const float rmsEarly = measureRms(kWin);

    // Continue feeding stimulus for ~5 s. Under the accumulation bug,
    // Freverb comb output grows linearly with elapsed samples at a rate
    // of ~(N / commbDelay) × input_mean — for the shortest comb
    // (~1557 samples at 44.1 kHz) this reaches ≈140× after 5 s. With
    // the tail correctly held (no new input into the frozen reverb),
    // RMS stays within a small window of its post-crossfade value.
    for (int i = 0; i < 44100 * 5 - kWin; ++i)
        rev.process(stim(t++));

    const float rmsLate = measureRms(kWin);

    INFO("rmsEarly=" << rmsEarly << " rmsLate=" << rmsLate
         << " ratio=" << rmsLate / rmsEarly);

    // Ceiling 2.0 safely separates a bounded hold (ratio ≈ 1, with slight
    // drift from damp2 rounding under fb=1.0) from linear accumulation
    // (ratio ≫ 5 even accounting for comb averaging).
    REQUIRE(rmsLate < rmsEarly * 2.0f);
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
