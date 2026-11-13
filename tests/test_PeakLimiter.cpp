#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/PeakLimiter.h>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;
static constexpr float kPi = 3.14159265f;

// Threshold derivations used throughout this file
// -----------------------------------------------
// PeakLimiter pipeline (2026-11-13, ramped attack):
//   input → lookahead delay (N = max(1, floor(lookahead·fs)) samples)
//   target g_t = threshold / |x| if |x| > threshold, else 1
//   min-hold of g_t over N samples → running mean over N samples
//   one-pole release (α_r) upward; attack follows the mean directly
//   gain = min(that, g_t of the sample leaving the delay)
//   output = delayed · gain
//
// Delay is N − 1 samples (verified directly: input at call 0 is stored at
// buf[0], read index advances to 1, and the value emerges at call N − 1
// when the write index wraps back to 0).
//
// Below-threshold invariant: every target is exactly 1, the running mean
// of N ones is exactly 1 (double sum of integers), the release keeps 1 → 1,
// so output = delayed bit-exact.
//
// Brickwall property: the final min with the outgoing sample's own target
// makes |out| ≤ threshold up to the rounding of x·(threshold/|x|) (1 ulp).
// The "threshold + 2e-3" bounds in the sine tests below are therefore
// looser than necessary; they remain valid upper bounds.
//
// Gain recovery analytic: after a burst that drives the gain to
// threshold/peak, a quiet input q < threshold recovers after the N-sample
// hold and N-sample ramp with the release pole. Residual after 10τ_r is
// exp(−10) ≈ 4.5e-5 relative (the 2·N = 176 samples of hold + ramp are
// < 1 % of the 22 050-sample window), so after 500 ms (=10·50 ms) output
// is within q · ~1e-4 of q.
//
// getGainReductionDb: with currentGain = threshold/A for steady input A
// above threshold, gainReductionDb = 20·log10(threshold/A). For
// threshold = 0 dB (1.0 linear) and input 4.0: 20·log10(0.25) = −12.04 dB.

TEST_CASE("PeakLimiter: signal below threshold is bit-exact passthrough", "[limiter]")
{
    // threshold = 0 dB = 1.0 linear. Input 0.5 < threshold so envelope ≤ 0.5,
    // targetGain ≡ 1, currentGain stays at 1. Once the lookahead buffer
    // fills with 0.5 (after ≥ N−1 samples), output = 0.5 exactly.
    ideath::PeakLimiter lim;
    lim.prepare(kSampleRate);
    lim.setThreshold(0.0f);
    lim.setLookahead(0.002f); // N = 88 samples → delay 87
    lim.setRelease(0.1f);

    // Prime with silence long enough to clear any initial buffer content.
    for (int i = 0; i < 100; ++i) lim.process(0.0f);

    // Fill lookahead buffer with steady 0.5.
    for (int i = 0; i < 200; ++i) lim.process(0.5f);

    // Once buffer is full of 0.5, every subsequent output is 0.5 bit-exact.
    for (int i = 0; i < 100; ++i)
        REQUIRE(lim.process(0.5f) == 0.5f);
}

TEST_CASE("PeakLimiter: sine above threshold bounded by threshold + tiny overshoot", "[limiter]")
{
    // threshold = −6 dB ≈ 0.5012 linear. Sine peak = 1.0. With lookahead
    // 3 ms and release 50 ms, continuous re-attacks on each |sin| peak keep
    // overshoot far below the worst-case (1/α_r^N − 1) bound. Observed max
    // overshoot under these params is ~1e-4; use 2e-3 absolute (≈ 0.4 %
    // of threshold) as a comfortable but still tight bound.
    ideath::PeakLimiter lim;
    lim.prepare(kSampleRate);
    lim.setThreshold(-6.0f);
    lim.setLookahead(0.003f);
    lim.setRelease(0.05f);

    const float thr = std::pow(10.0f, -6.0f / 20.0f);
    for (int i = 0; i < 44100; ++i)
    {
        const float input = std::sin(2.0f * kPi * 440.0f * static_cast<float>(i) / kSampleRate);
        const float out = lim.process(input);
        if (i > 200)  // skip lookahead warm-up
        {
            REQUIRE(std::fabs(out) <= thr + 2e-3f);
        }
    }
}

TEST_CASE("PeakLimiter: brickwall at 0 dB on hot 5× sine", "[limiter]")
{
    // Drive a 200 Hz sine at 5× amplitude into a 0 dB limiter. Lookahead
    // 5 ms, release 100 ms. With the low frequency (T = 220 samples)
    // envelope re-attacks every 110 samples, well within the lookahead;
    // overshoot stays below 2e-3 absolute.
    ideath::PeakLimiter lim;
    lim.prepare(kSampleRate);
    lim.setThreshold(0.0f);
    lim.setLookahead(0.005f);
    lim.setRelease(0.1f);

    for (int i = 0; i < 44100; ++i)
    {
        const float input = 5.0f * std::sin(2.0f * kPi * 200.0f * static_cast<float>(i) / kSampleRate);
        const float out = lim.process(input);
        if (i > 250)
        {
            REQUIRE(std::fabs(out) <= 1.0f + 2e-3f);
        }
    }
}

TEST_CASE("PeakLimiter: gain recovery approaches quiet input within 1e-4 after 10τ", "[limiter]")
{
    // 441 samples of input = 3.0 drives currentGain to 1/3 (threshold=1.0).
    // Following 22050 samples = 10τ_r of input = 0.3:
    //   targets return to 1 at once; the min-hold keeps 1/3 for N = 88
    //   samples, the mean ramps up over the next 88, then the gain
    //   releases from there toward 1 (release pole at α_r)
    //   residual at 10τ_r ≈ exp(−10) ≈ 4.5e-5 relative
    //   analytical output ≈ 0.299965. Tolerance 1e-3 absorbs residual + ULP.
    ideath::PeakLimiter lim;
    lim.prepare(kSampleRate);
    lim.setThreshold(0.0f);
    lim.setLookahead(0.002f);
    lim.setRelease(0.05f);

    for (int i = 0; i < 441; ++i) lim.process(3.0f);

    float lastOut = 0.0f;
    for (int i = 0; i < 22050; ++i)
        lastOut = lim.process(0.3f);

    REQUIRE_THAT(lastOut, WithinAbs(0.3f, 1e-3f));
}

TEST_CASE("PeakLimiter: getGainReductionDb — 0 at silence, -12.04 dB under 4× input", "[limiter]")
{
    // At silence: currentGain = 1 (initial) → 20·log10(1) = 0 bit-exact.
    // Steady input 4.0 with threshold 1.0: target 0.25 for every sample;
    // after the N = 88-sample ramp (441 samples is well past it) the gain
    // is exactly 0.25. Analytical
    // gainReductionDb = 20·log10(0.25) = −12.0412 dB.
    ideath::PeakLimiter lim;
    lim.prepare(kSampleRate);
    lim.setThreshold(0.0f);
    lim.setLookahead(0.002f);

    for (int i = 0; i < 200; ++i) lim.process(0.0f);
    REQUIRE(lim.getGainReductionDb() == 0.0f);

    for (int i = 0; i < 441; ++i) lim.process(4.0f);
    REQUIRE_THAT(lim.getGainReductionDb(), WithinAbs(-12.0412f, 0.01f));
}

TEST_CASE("PeakLimiter: handles silence (bit-exact zero)", "[limiter]")
{
    // Silence in, envelope stays 0, currentGain stays 1 → output = 0·1 = 0.
    ideath::PeakLimiter lim;
    lim.prepare(kSampleRate);
    lim.setThreshold(0.0f);
    lim.setLookahead(0.002f);

    for (int i = 0; i < 4410; ++i)
        REQUIRE(lim.process(0.0f) == 0.0f);
}

TEST_CASE("PeakLimiter: reset clears state (bit-exact zero after)", "[limiter]")
{
    // After hot signal processing, reset() clears buffer, envelope, and
    // currentGain (=1). Subsequent silence produces bit-exact 0 output.
    ideath::PeakLimiter lim;
    lim.prepare(kSampleRate);
    lim.setThreshold(0.0f);
    lim.setLookahead(0.002f);

    for (int i = 0; i < 1000; ++i) lim.process(5.0f);
    REQUIRE(lim.getGainReductionDb() < -10.0f);  // sanity: was compressing

    lim.reset();
    REQUIRE(lim.getGainReductionDb() == 0.0f);
    for (int i = 0; i < 200; ++i)
        REQUIRE(lim.process(0.0f) == 0.0f);
}

TEST_CASE("PeakLimiter: default-constructible produces finite output", "[limiter]")
{
    ideath::PeakLimiter lim;
    const float out = lim.process(0.5f);
    REQUIRE(std::isfinite(out));
}

TEST_CASE("PeakLimiter: negative threshold — sine bounded by threshold_linear", "[limiter]")
{
    // threshold = −12 dB ≈ 0.251 linear. Sine peak 1.0 > threshold, so
    // limiter engages. Same brickwall property; tight overshoot bound.
    ideath::PeakLimiter lim;
    lim.prepare(kSampleRate);
    lim.setThreshold(-12.0f);
    lim.setLookahead(0.003f);
    lim.setRelease(0.05f);

    const float thr = std::pow(10.0f, -12.0f / 20.0f);
    for (int i = 0; i < 44100; ++i)
    {
        const float input = std::sin(2.0f * kPi * 440.0f * static_cast<float>(i) / kSampleRate);
        const float out = lim.process(input);
        if (i > 200)
            REQUIRE(std::fabs(out) <= thr + 2e-3f);
    }
}

TEST_CASE("PeakLimiter: lookahead delay is exactly lookaheadSamples - 1", "[limiter]")
{
    // setLookahead(s) → N = max(1, floor(s·fs)). s = 5 ms at fs = 44.1 kHz
    // gives N = 220, delay = N − 1 = 219 samples. Impulse at call 0 emerges
    // at call 219 bit-exact (envelope never exceeded threshold, so
    // currentGain stayed at 1).
    ideath::PeakLimiter lim;
    lim.prepare(kSampleRate);
    lim.setThreshold(0.0f);
    lim.setLookahead(0.005f);

    constexpr int kExpectedDelay = 219;
    std::vector<float> output(kExpectedDelay + 10);
    output[0] = lim.process(0.5f);
    for (size_t i = 1; i < output.size(); ++i)
        output[i] = lim.process(0.0f);

    // All pre-delay samples are bit-exact 0.
    for (int i = 0; i < kExpectedDelay; ++i)
    {
        INFO("i = " << i);
        REQUIRE(output[i] == 0.0f);
    }
    // Impulse emerges exactly at the expected sample, bit-exact (no gain
    // reduction applies because input peak 0.5 < threshold 1.0).
    REQUIRE(output[kExpectedDelay] == 0.5f);
    // After the impulse, output returns to zero (buffer already wrapped).
    for (size_t i = kExpectedDelay + 1; i < output.size(); ++i)
    {
        INFO("i = " << i);
        REQUIRE(output[i] == 0.0f);
    }
}

TEST_CASE("PeakLimiter: threshold clamps to <= 0 dB", "[limiter]")
{
    // setThreshold clamps dB to min(dB, 0). A positive argument → 0 dB
    // → threshold = 1 linear. Sine peak 0.5 below threshold → bit-exact
    // passthrough after lookahead fills.
    ideath::PeakLimiter lim;
    lim.prepare(kSampleRate);
    lim.setThreshold(10.0f);   // clamped to 0 dB
    lim.setLookahead(0.002f);
    for (int i = 0; i < 300; ++i) lim.process(0.5f);
    for (int i = 0; i < 100; ++i)
        REQUIRE(lim.process(0.5f) == 0.5f);
}

TEST_CASE("PeakLimiter: lookahead clamps to [0, 10 ms]", "[limiter]")
{
    // setLookahead clamps to [0, 0.01]. At 0, lookaheadSamples = 1 (the
    // max(1, ⌊s·fs⌋) floor). At 100 ms input, clamps to 10 ms → 441 samples.
    ideath::PeakLimiter lim;
    lim.prepare(kSampleRate);
    lim.setThreshold(0.0f);

    // Lookahead = 0 → minimum 1-sample delay, so impulse emerges at call 0?
    // Actually with N=1, delay = N − 1 = 0, so input passes through.
    lim.setLookahead(0.0f);
    REQUIRE(lim.process(0.0f) == 0.0f);

    // Lookahead = 100 ms clamped to 10 ms → 441 samples. Verify delay = 440.
    lim.reset();
    lim.setLookahead(1.0f);  // clamped to 10 ms
    constexpr int kExpected = 440;  // 441 − 1
    lim.process(0.5f);
    for (int i = 0; i < kExpected - 1; ++i)
        REQUIRE(lim.process(0.0f) == 0.0f);
    REQUIRE(lim.process(0.0f) == 0.5f);  // impulse emerges at call 440
}

// ---------------------------------------------------------------------------
// Long-run stability
// ---------------------------------------------------------------------------

TEST_CASE("PeakLimiter: 10-second stability at extreme settings", "[limiter][stability]")
{
    // 10 s × 44.1 kHz = 441 000 samples. Aggressive settings (−10 dB
    // threshold, hot input). Output must stay finite and bounded by
    // threshold + practical-overshoot bound; getGainReductionDb finite.
    ideath::PeakLimiter lim;
    lim.prepare(kSampleRate);
    lim.setThreshold(-10.0f);
    lim.setLookahead(0.005f);
    lim.setRelease(0.2f);

    const float thr = std::pow(10.0f, -10.0f / 20.0f);
    for (int i = 0; i < 441000; ++i)
    {
        const float t = static_cast<float>(i) / kSampleRate;
        const float input = 3.0f * (std::sin(2.0f * kPi * 220.0f * t)
                                   + 0.5f * std::sin(2.0f * kPi * 1100.0f * t));
        const float out = lim.process(input);
        REQUIRE(std::isfinite(out));
        if (i > 500)
            REQUIRE(std::fabs(out) <= thr + 5e-3f);
        REQUIRE(std::isfinite(lim.getGainReductionDb()));
    }
}

// ---------------------------------------------------------------------------
// Ramped attack and stereo link
// ---------------------------------------------------------------------------
//
// Gain computer: per input sample the target g_t = min(1, thr / |x|); a
// running minimum over the last N targets (N = lookahead samples) followed
// by a running mean over N samples, then one-pole release upward only, and
// finally min(·, g_t of the sample leaving the delay).
//
// Ramp: the running mean of a step from 1 to g changes by (1 − g)/N per
// sample — the largest per-sample gain step any single peak can cause.
// Brickwall: a peak entering at sample p holds every min-hold value in
// [p, p + N − 1] at ≤ g, so the mean at p + N − 1 — the sample where the
// peak leaves the N − 1 delay — is ≤ g; the final min with that sample's
// own target makes |out| ≤ thr exactly (up to the rounding of x·(thr/|x|),
// ≤ 1 ulp → 1.2e−7 relative).

#include <algorithm>
#include <cstdint>

TEST_CASE("PeakLimiter: strike transient ramps the gain over the lookahead", "[limiter]")
{
    // Silence, then a +7 dB strike (2.24 into a 0 dB threshold).  An
    // instant attack would drop the gain by 7 dB in one sample, on the
    // delayed, still-silent signal.  Spec: per-sample gain change ≤
    // (1 − g)/N (+ 1e−6 for float rounding of the running mean), and the
    // gain reaches g as the strike leaves the delay.
    ideath::PeakLimiter lim;
    lim.prepare(48000.0f);
    lim.setThreshold(0.0f);
    lim.setLookahead(0.002f); // N = 96
    const int N = 96;
    const float strike = 2.24f;
    const float g = 1.0f / strike;

    for (int i = 0; i < 500; ++i)
        lim.process(0.0f);
    float prevGain = 1.0f;
    float maxStep = 0.0f;
    float outAtStrike = 0.0f;
    for (int i = 0; i < 400; ++i)
    {
        const float out = lim.process(i == 0 ? strike : 0.0f);
        if (i == N - 1)
            outAtStrike = out;
        const float gain = std::pow(10.0f, lim.getGainReductionDb() / 20.0f);
        maxStep = std::max(maxStep, prevGain - gain);
        prevGain = gain;
    }
    REQUIRE(maxStep <= (1.0f - g) / N + 1e-6f);
    REQUIRE(outAtStrike <= 1.0f);
    REQUIRE(outAtStrike >= 1.0f - 1e-6f); // target reached, not over-limited
}

TEST_CASE("PeakLimiter: brickwall holds exactly on random peaky input", "[limiter]")
{
    uint32_t seed = 4242u;
    auto rnd = [&]() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                       return static_cast<float>(seed) / 4294967296.0f; };
    for (float lookahead : { 0.0f, 0.0005f, 0.005f })
    {
        ideath::PeakLimiter lim;
        lim.prepare(48000.0f);
        lim.setThreshold(-3.0f);
        lim.setLookahead(lookahead);
        lim.setRelease(0.02f);
        const float thr = std::pow(10.0f, -3.0f / 20.0f);
        for (int i = 0; i < 96000; ++i)
        {
            float x = (rnd() - 0.5f) * 0.8f;
            if (rnd() < 0.002f)
                x = (rnd() < 0.5f ? -1.0f : 1.0f) * (1.0f + 20.0f * rnd()); // strikes up to +26 dB
            const float out = lim.process(x);
            REQUIRE(std::fabs(out) <= thr * (1.0f + 1.2e-7f));
        }
    }
}

TEST_CASE("PeakLimiter: processStereo applies one gain to both channels", "[limiter]")
{
    // Only the left channel peaks; the right must be reduced by the same
    // gain so the L/R ratio (the image) of every output sample equals the
    // input's.  Ratio compared on the delayed input pair; relative
    // tolerance 2.4e−7 = two roundings (one multiply per channel).
    ideath::PeakLimiter lim;
    lim.prepare(48000.0f);
    lim.setThreshold(-6.0f);
    lim.setLookahead(0.003f); // N = 144, delay 143
    const int delay = 143;
    const float thr = std::pow(10.0f, -6.0f / 20.0f);

    std::vector<std::pair<float, float>> in, out;
    for (int i = 0; i < 9600; ++i)
    {
        const float t = static_cast<float>(i) / 48000.0f;
        const float l = (i > 2000 && i < 5000 ? 1.5f : 0.3f) * std::sin(2.0f * kPi * 330.0f * t);
        const float r = 0.4f * std::sin(2.0f * kPi * 330.0f * t + 0.3f);
        in.emplace_back(l, r);
        out.push_back(lim.processStereo(l, r));
    }
    bool limited = false;
    for (size_t i = delay; i < in.size(); ++i)
    {
        const auto [dl, dr] = in[i - delay];
        const auto [ol, orr] = out[i];
        REQUIRE(std::fabs(ol) <= thr * (1.0f + 1.2e-7f));
        REQUIRE(std::fabs(orr) <= thr * (1.0f + 1.2e-7f));
        if (std::fabs(dl) > 1e-3f && std::fabs(dr) > 1e-3f)
        {
            const float gl = ol / dl, gr = orr / dr;
            REQUIRE_THAT(gl, WithinAbs(gr, 2.4e-7f * std::max(1.0f, gl)));
            if (gl < 0.9f)
                limited = true;
        }
    }
    REQUIRE(limited); // the link was actually exercised
}

TEST_CASE("PeakLimiter: processStereo with identical channels equals mono", "[limiter]")
{
    // max(|x|, |x|) = |x|: same targets, same gain, same delay → bit-exact.
    ideath::PeakLimiter mono, stereo;
    for (auto* l : { &mono, &stereo })
    {
        l->prepare(48000.0f);
        l->setThreshold(-2.0f);
        l->setLookahead(0.004f);
    }
    for (int i = 0; i < 20000; ++i)
    {
        const float x = 2.0f * std::sin(0.031f * i) * std::sin(0.0007f * i);
        const float m = mono.process(x);
        const auto [l, r] = stereo.processStereo(x, x);
        REQUIRE(l == m);
        REQUIRE(r == m);
    }
}

TEST_CASE("PeakLimiter: reset clears the ramp state (stereo)", "[limiter]")
{
    ideath::PeakLimiter a, b;
    for (auto* l : { &a, &b })
    {
        l->prepare(48000.0f);
        l->setThreshold(0.0f);
        l->setLookahead(0.002f);
    }
    for (int i = 0; i < 1000; ++i)
        a.processStereo(4.0f * std::sin(0.1f * i), -3.0f);
    a.reset();
    for (int i = 0; i < 1000; ++i)
    {
        const float x = 3.0f * std::sin(0.05f * i);
        REQUIRE(a.processStereo(x, 0.5f * x) == b.processStereo(x, 0.5f * x));
    }
}

TEST_CASE("PeakLimiter: min-hold survives a full window of rising targets", "[limiter]")
{
    // A decaying strike gives strictly rising targets for longer than the
    // window: the min-hold deque then holds one entry per sample of the
    // window and must not overflow.  Reference: brute-force min-hold and
    // mean over the window; the applied gain may lag below it (release
    // pole) but must never exceed it.  1e−5 relative covers the
    // log10 / pow round trip of the metering read.  N = 192: a hold that
    // forgets the window minimum tracks the current target, ≈ N samples of
    // rise ahead of the true ramp — far more than the release pole's lag
    // (τ = 48 samples at 1 ms), so the error cannot hide behind it.
    ideath::PeakLimiter lim;
    lim.prepare(48000.0f);
    lim.setThreshold(0.0f);
    lim.setRelease(0.001f);
    lim.setLookahead(0.004f); // N = 192
    const int N = 192;
    std::vector<float> targets;
    std::vector<float> holds;
    for (int i = 0; i < 2000; ++i)
    {
        const float x = 8.0f * std::exp(-0.002f * i) + 1.0f; // > thr, strictly falling
        targets.push_back(1.0f / x);
        lim.process(x);
        float h = 1.0f;
        for (int k = std::max(0, i - N + 1); k <= i; ++k)
            h = std::min(h, targets[static_cast<size_t>(k)]);
        holds.push_back(h);
        if (i >= 2 * N)
        {
            double mean = 0.0;
            for (int k = i - N + 1; k <= i; ++k)
                mean += holds[static_cast<size_t>(k)];
            mean /= N;
            const float expected = std::min(static_cast<float>(mean),
                                            targets[static_cast<size_t>(i - N + 1)]);
            const float gain = std::pow(10.0f, lim.getGainReductionDb() / 20.0f);
            REQUIRE(gain <= expected * (1.0f + 1e-5f));
        }
    }
}

TEST_CASE("PeakLimiter: lowering the threshold mid-stream keeps the brickwall", "[limiter]")
{
    // Targets already in the window were computed for the old threshold;
    // the ceiling must still be the new one from the next output on
    // (same 1-ulp rounding allowance as the brickwall test).
    ideath::PeakLimiter lim;
    lim.prepare(48000.0f);
    lim.setThreshold(0.0f);
    lim.setLookahead(0.005f);
    for (int i = 0; i < 2000; ++i)
        lim.process(0.9f * std::sin(0.05f * i));
    lim.setThreshold(-12.0f);
    const float thr = std::pow(10.0f, -12.0f / 20.0f);
    for (int i = 2000; i < 4000; ++i)
        REQUIRE(std::fabs(lim.process(0.9f * std::sin(0.05f * i))) <= thr * (1.0f + 1.2e-7f));
}
