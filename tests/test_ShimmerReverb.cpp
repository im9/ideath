#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/ShimmerReverb.h>
#include <ideath/Biquad.h>
#include <algorithm>
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

// Sum of Goertzel bins (bin spacing sr / n) across [loHz, hiHz].  The
// LFO-modulated allpasses Doppler-spread every partial: at size 0.7 the
// read offset swings ±110 samples at 0.7–1.5 Hz → ±1.1–2.4 % per stage,
// ≈ ±10 % after the four stages of one loop pass.  A ±10 % band therefore
// captures a partial's energy where a single bin would miss most of it.
static double bandPower(const float* x, int n, float loHz, float hiHz, float sr)
{
    double e = 0.0;
    const float step = sr / static_cast<float>(n);
    for (float f = loHz; f <= hiHz; f += step)
        e += goertzelPower(x, n, f, sr);
    return e;
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

    const double powIn = bandPower(outL.data(), kAnalyse, 200.0f, 240.0f, kSampleRate);
    const double powOct = bandPower(outL.data(), kAnalyse, 400.0f, 480.0f, kSampleRate);

    INFO("input bin = " << powIn << "  octave bin = " << powOct);
    // Expected ratio: size 0.7 → nominal T60 = 0.5·(29.6/0.5)^0.7 ≈ 8.7 s
    // → loop gain g = 10^(−3·0.311/8.7) ≈ 0.78.  At shimmer 1 every pass is
    // shifted; the two Hann taps carry ≈ 0.75 of the power, so the octave
    // returns at amplitude ≈ 0.78·√0.75 ≈ 0.68 of the forward-path tone.
    // It first arrives after one loop + mean shifter delay ≈ 0.311 + 0.023
    // = 0.334 s, i.e. for (0.557 − 0.334) / 0.371 ≈ 60 % of the analysis
    // window (0.186–0.557 s) → power ratio ≈ 0.68² · 0.6 ≈ 0.28.  A loop
    // that does not shift pitch yields ≈ 0 (only modulation sidebands).
    // 0.02 sits > 10 dB below the 0.28 estimate (allowing for Doppler
    // spread beyond the ±10 % bands) and far above the non-shifting floor.
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
    // ≈ 1e−25 / (1 − kFeedback=0.5) = 2e−25.  The allpass network
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

    // Lower bound on the first delayed arrival: at size 0.7 the forward
    // allpass gains are g1 = 0.35·0.85 ≈ 0.30 and g2 = 0.375·0.85 ≈ 0.32.
    // When the first allpass's delayed tap returns (n ≈ 601) its output is
    // 1 − g1² ≈ 0.91, and the second stage passes −g2·0.91 ≈ −0.29 straight
    // through.  Linear interpolation can split it over two samples (≥ ½ on
    // one), the default damp 0.3 one-pole passes (1 − c) ≈ 0.39 of it on that
    // sample, and the wet bus scales by 3 → peak ≥ 0.29·0.5·0.39·3 ≈ 0.17.
    // 0.01 is > 10× below that bound; a dead network gives 0.
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
        // L and R run independent loops with different allpass and delay
        // lengths (e.g. forward APs 601/613 vs 2043/2087), so their first
        // arrivals land on different samples.  0.001 is > 100× below the
        // ≥ 0.17 first-arrival peak bound in "impulse produces stereo tail".
        if (std::fabs(l - r) > 0.001f)
        {
            differ = true;
            break;
        }
    }
    REQUIRE(differ);
}

TEST_CASE("ShimmerReverb: shimmer amount moves the tail to the octave", "[shimmer]")
{
    // Shimmer is the octave-up share of the feedback signal.  At 0 nothing
    // in the loop shifts pitch, so a 220 Hz input leaves the 440 Hz band
    // nearly empty (only modulation sidebands leak there); at 1 every pass
    // is shifted up.
    auto octaveRatio = [](float shimmerAmt) {
        ideath::ShimmerReverb rev;
        rev.prepare(kSampleRate);
        rev.setSize(0.7f);
        rev.setDamp(0.0f);
        rev.setShimmer(shimmerAmt);
        rev.setMix(1.0f);

        constexpr int kSettle = 8192;
        constexpr int kAnalyse = 16384;
        const float wIn = 2.0f * static_cast<float>(M_PI) * 220.0f / kSampleRate;
        for (int i = 0; i < kSettle; ++i)
            rev.process(0.5f * std::sin(wIn * static_cast<float>(i)));
        std::vector<float> out(kAnalyse);
        for (int i = 0; i < kAnalyse; ++i)
            out[i] = rev.process(0.5f * std::sin(wIn * static_cast<float>(kSettle + i))).first;

        return bandPower(out.data(), kAnalyse, 400.0f, 480.0f, kSampleRate)
             / bandPower(out.data(), kAnalyse, 200.0f, 240.0f, kSampleRate);
    };

    const double none = octaveRatio(0.0f);
    const double full = octaveRatio(1.0f);
    INFO("octave/input band ratio: shimmer 0 = " << none << ", 1 = " << full);
    // 10× (10 dB) separates "an octave path exists" from sideband leakage.
    REQUIRE(full > 10.0 * none);
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
    // 1 s blocks: see the threshold derivation below.
    float energy1 = 0.0f;
    for (int i = 0; i < 44100; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        energy1 += l * l + r * r;
    }

    float energy2 = 0.0f;
    for (int i = 0; i < 44100; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        energy2 += l * l + r * r;
    }

    // After the 200 ms crossfade only the frozen Freeverb is heard: combs at
    // fb = 1 with damp 0 and muted input are lossless, so each comb's output
    // is periodic with its own length L ≤ 1617 + 23 = 1640 samples, and the
    // Freeverb allpasses after them are LTI.  A block's energy can then fall
    // short of the previous block's only by the partial period at its edge:
    // ≤ L / N = 1640 / 44100 ≈ 3.7 % for 1 s blocks.  0.95 = 1 − 3.7 %
    // rounded down by ~1 % for float recirculation over 2 s.
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
        // Allpass network + pitch-shifted feedback can build
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

    // The burst injects ≈ 441 · (0.5 · 3)² ≈ 1e3 of wet energy per channel
    // on its first pass.  By 1–2 s (passes 3–6 of the 0.31 s loop) a loop
    // at g = 0.93 with the shifter's 0.75 power gain keeps ≥ (0.93²·0.75)^6
    // ≈ 0.07 of it per channel before DC-blocker and anti-alias losses, i.e.
    // order 10².  0.01 is four orders below that and is failed only by a
    // loop that does not recirculate (the ~50 ms forward diffusion alone
    // leaves nothing after 1 s).
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

    // 0.1s signal then 10s silence. Tests the allpass network,
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

// ---------------------------------------------------------------------------
// Size → decay length, stability, and damp evenness
// ---------------------------------------------------------------------------

static constexpr float kNoteSec = 0.26f;

// 220 Hz naive saw at 0.5 amplitude for kNoteSec, then silence.  The saw's
// harmonics cover the whole band, so both the decay and the damping
// measurements see broadband material.
static float shimmerStim(int i)
{
    if (i >= static_cast<int>(kNoteSec * kSampleRate))
        return 0.0f;
    const float ph = std::fmod(220.0f * static_cast<float>(i) / kSampleRate, 1.0f);
    return 0.5f * (2.0f * ph - 1.0f);
}

// Renders `out.size()` samples of mono-summed wet output for the stimulus.
static void renderShimmer(std::vector<float>& out, float size, float damp,
                          float shimmer)
{
    ideath::ShimmerReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(size);
    rev.setDamp(damp);
    rev.setShimmer(shimmer);
    rev.setMix(1.0f);
    for (int i = 0; i < static_cast<int>(out.size()); ++i)
    {
        auto [l, r] = rev.process(shimmerStim(i));
        out[i] = 0.5f * (l + r);
    }
}

// T60 from the Schroeder energy-decay curve of the tail after release:
// least-squares line through the −5 … −35 dB span (T30), extrapolated to
// −60 dB.  This is the ISO 3382 T30 method.
static double shimmerT60(const std::vector<float>& y, std::vector<double>& edc)
{
    const int rel = static_cast<int>(kNoteSec * kSampleRate);
    const int n = static_cast<int>(y.size()) - rel;
    double acc = 0.0;
    for (int i = n - 1; i >= 0; --i)
    {
        acc += static_cast<double>(y[rel + i]) * y[rel + i];
        edc[i] = acc;
    }
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    int count = 0;
    for (int i = 0; i < n; i += 64)
    {
        const double db = 10.0 * std::log10(edc[i] / edc[0] + 1e-30);
        if (db <= -5.0 && db >= -35.0)
        {
            const double t = i / static_cast<double>(kSampleRate);
            sx += t; sy += db; sxx += t * t; sxy += t * db; ++count;
        }
    }
    if (count < 2)
        return 0.0;
    const double slope = (count * sxy - sx * sy) / (count * sxx - sx * sx);
    return -60.0 / slope;
}

TEST_CASE("ShimmerReverb: size scales decay time monotonically", "[shimmer]")
{
    // Default damp / shimmer (0.3 / 0.5) — the operating point a host that
    // only drives size and damp sees.  30 s render: long enough for the EDC
    // to cross −35 dB at the largest size.
    std::vector<float> y(static_cast<int>(30.0f * kSampleRate));
    std::vector<double> edc(y.size());

    const float sizes[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    double t60[5];
    for (int k = 0; k < 5; ++k)
    {
        renderShimmer(y, sizes[k], 0.3f, 0.5f);
        t60[k] = shimmerT60(y, edc);
        INFO("size " << sizes[k] << " T60 " << t60[k]);
        CHECK(t60[k] > 0.0);
    }

    for (int k = 1; k < 5; ++k)
    {
        INFO("size " << sizes[k - 1] << " → " << sizes[k] << ": "
             << t60[k - 1] << " s → " << t60[k] << " s");
        REQUIRE(t60[k] > t60[k - 1]);
    }
    // 5× = the ratio at which a short room and a long wash are clearly
    // distinct spaces (e.g. ~0.5 s small room vs ≥2.5 s hall).
    INFO("T60 ratio size 1 / size 0 = " << t60[4] / t60[0]);
    REQUIRE(t60[4] >= 5.0 * t60[0]);
}

TEST_CASE("ShimmerReverb: max size does not self-oscillate", "[shimmer]")
{
    // Tail energy must fall, never grow, at the 0.93 loop-gain ceiling with
    // no damping, for pure-direct, blended, and pure-octave feedback.
    // Floor for the fall between the 1–2 s and 19–20 s windows: the loop
    // loses at least −20·log10(0.93) = 0.63 dB per pass; the longer loop
    // (R, ≈ 0.32 s) completes 18 / 0.32 ≈ 56 passes → ≥ 35 dB.  The 30 dB
    // requirement leaves 5 dB for window-to-window energy fluctuation from
    // the LFO-modulated allpasses.
    std::vector<float> y(static_cast<int>(21.0f * kSampleRate));
    const int rel = static_cast<int>(kNoteSec * kSampleRate);
    const auto windowDb = [&](float from, float to) {
        const int a = rel + static_cast<int>(from * kSampleRate);
        const int b = rel + static_cast<int>(to * kSampleRate);
        double e = 0.0;
        for (int i = a; i < b; ++i)
            e += static_cast<double>(y[i]) * y[i];
        return 10.0 * std::log10(e / (b - a) + 1e-30);
    };

    for (float shimmer : { 0.0f, 0.5f, 1.0f })
    {
        renderShimmer(y, 1.0f, 0.0f, shimmer);
        float peak = 0.0f;
        for (float v : y)
            peak = std::max(peak, std::fabs(v));
        const double early = windowDb(1.0f, 2.0f);
        const double late = windowDb(19.0f, 20.0f);
        INFO("shimmer " << shimmer << " early " << early << " dB, late "
             << late << " dB, peak " << peak);
        // ±6.0 = documented ShimmerReverb output ceiling (CLAUDE.md).
        REQUIRE(peak <= 6.0f);
        REQUIRE(late <= early - 30.0);
    }
}

TEST_CASE("ShimmerReverb: damp darkens evenly across its range", "[shimmer]")
{
    // HF (> 4 kHz, 4th-order HP) tail energy 0.3–1.3 s after release at
    // damp = 0, 0.1, …, 1.  Every 0.1 step must darken the tail, and no
    // step may darken it by more than twice the mean step — "even" means
    // the response never doubles its rate partway through the knob (the
    // former linear-coefficient mapping dropped ~20× the mean in its final
    // step).
    std::vector<float> y(static_cast<int>((kNoteSec + 1.3f) * kSampleRate));
    const int rel = static_cast<int>(kNoteSec * kSampleRate);
    const int a = rel + static_cast<int>(0.3f * kSampleRate);
    const int b = static_cast<int>(y.size());

    double hf[11];
    for (int k = 0; k <= 10; ++k)
    {
        renderShimmer(y, 0.7f, k / 10.0f, 0.5f);
        ideath::Biquad hp1, hp2;
        hp1.setHighpass(4000.0f, 0.7071f, kSampleRate);
        hp2.setHighpass(4000.0f, 0.7071f, kSampleRate);
        double e = 0.0;
        for (int i = 0; i < b; ++i)
        {
            const float v = hp2.process(hp1.process(y[i]));
            if (i >= a)
                e += static_cast<double>(v) * v;
        }
        hf[k] = 10.0 * std::log10(e / (b - a) + 1e-30);
    }

    const double meanStep = (hf[10] - hf[0]) / 10.0;
    INFO("HF dB damp 0 = " << hf[0] << ", damp 1 = " << hf[10]
         << ", mean step " << meanStep);
    REQUIRE(meanStep < 0.0);
    for (int k = 1; k <= 10; ++k)
    {
        const double step = hf[k] - hf[k - 1];
        INFO("damp " << (k - 1) / 10.0 << " → " << k / 10.0 << " step " << step);
        REQUIRE(step < 0.0);
        REQUIRE(step >= 2.0 * meanStep);
    }
}

TEST_CASE("ShimmerReverb: reset matches a freshly prepared instance", "[shimmer]")
{
    // reset() must return every piece of state — buffers, filter memories
    // and the allpass LFO phases — to what prepare() leaves, so the same
    // input gives a bit-identical output afterwards.
    ideath::ShimmerReverb fresh;
    ideath::ShimmerReverb used;
    fresh.prepare(kSampleRate);
    used.prepare(kSampleRate);

    for (int i = 0; i < 10000; ++i)
        used.process(i < 100 ? 0.5f : 0.0f);
    used.reset();

    int mismatches = 0;
    for (int i = 0; i < 44100; ++i)
    {
        const float x = (i == 0) ? 1.0f : 0.0f;
        auto [fl, fr] = fresh.process(x);
        auto [ul, ur] = used.process(x);
        if (fl != ul || fr != ur)
            ++mismatches;
    }
    INFO("samples differing after reset: " << mismatches);
    REQUIRE(mismatches == 0);
}

// Fraction of the wet output's energy outside the carrier bin for a steady
// 100 Hz tone at size 0 (loop gain ≈ 10^(−3·0.31/0.5) ≈ 0.014, so the
// output is effectively one pass through the modulated allpasses).  The
// tone sits exactly on a bin of the 4 s window (0.25 Hz spacing), so the
// carrier itself does not leak; everything else is phase-modulation
// sideband energy at multiples of the 0.7–1.5 Hz LFO rates.
static double shimmerSidebandFraction(float sampleRate)
{
    ideath::ShimmerReverb rev;
    rev.prepare(sampleRate);
    rev.setSize(0.0f);
    rev.setShimmer(0.0f);
    rev.setDamp(0.0f);
    rev.setMix(1.0f);

    const float w = 2.0f * static_cast<float>(M_PI) * 100.0f / sampleRate;
    const int settle = static_cast<int>(1.0f * sampleRate);
    const int n = static_cast<int>(4.0f * sampleRate);
    for (int i = 0; i < settle; ++i)
        rev.process(0.3f * std::sin(w * static_cast<float>(i)));

    std::vector<float> y(n);
    double total = 0.0;
    for (int i = 0; i < n; ++i)
    {
        y[i] = rev.process(0.3f * std::sin(w * static_cast<float>(settle + i))).first;
        total += static_cast<double>(y[i]) * y[i];
    }
    // An on-bin tone of amplitude A has Goertzel power (n·A/2)² and signal
    // energy n·A²/2, so carrier fraction = power / (n/2 · Σx²).
    const double carrier = goertzelPower(y.data(), n, 100.0f, sampleRate);
    return 1.0 - carrier / (0.5 * n * total);
}

TEST_CASE("ShimmerReverb: modulation depth scales with sample rate", "[shimmer]")
{
    // Delay lengths scale with sample rate, so the LFO depth must too.
    // At 100 Hz the per-stage modulation index is β = 2π·f·D ≈ 0.35 for the
    // size-0 depth D ≈ 0.56 ms — the small-β regime, where sideband power
    // ≈ β²/2 ∝ D².  A depth fixed in samples changes D in time by
    // r = 44.1 kHz / sr, so the sideband fraction moves by r²; a depth fixed
    // in time leaves it unchanged.  The threshold is the geometric midpoint
    // √(r²) = r (or 1/r) between the two hypotheses.  Below ~11 kHz a fixed
    // depth also exceeds the shortest allpass delay and the read pointer
    // wraps, which spreads far more energy out of the carrier.
    const double ref = shimmerSidebandFraction(44100.0f);
    for (float sr : { 8000.0f, 22050.0f, 48000.0f, 96000.0f })
    {
        const double frac = shimmerSidebandFraction(sr);
        const double r = (sr > 44100.0f) ? sr / 44100.0 : 44100.0 / sr;
        const double ratio = std::max(frac / ref, ref / frac);
        INFO("sideband fraction at " << sr << " Hz = " << frac
             << " vs 44.1 kHz = " << ref << " (ratio " << ratio
             << ", limit " << r << ")");
        CHECK(ratio < r);
    }
}
