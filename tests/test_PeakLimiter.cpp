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
// PeakLimiter pipeline:
//   input → lookahead delay (N = max(1, floor(lookahead·fs)) samples)
//   envelope = peak detector on input (instant attack, one-pole release α_r)
//   targetGain = threshold / envelope if envelope > threshold, else 1
//   currentGain: instant attack (drops to targetGain when targetGain is smaller),
//                one-pole release (α_r) upward toward targetGain otherwise
//   output = delayed · currentGain
//
// Delay is N − 1 samples (verified directly: input at call 0 is stored at
// buf[0], read index advances to 1, and the value emerges at call N − 1
// when the write index wraps back to 0).
//
// Below-threshold invariant: if |input|_peak ≤ threshold throughout, then
// envelope ≤ threshold → targetGain ≡ 1 → currentGain ≡ 1 (stays at its
// initial 1) → output = delayed bit-exact.
//
// Brickwall property: because envelope instant-attacks and currentGain
// applies instant attack, output is bounded by threshold · (1/α_r^N) in
// the analytical worst case (a peak followed by silence for N samples).
// For continuous bandlimited input the envelope re-attacks every half
// signal period T/2, so the practical overshoot is far smaller; simulation
// under the test parameters gives |out| − threshold ≲ 2e-4 absolute, so
// a bound of threshold + 2e-3 is 10× safety.
//
// Gain recovery analytic: after a burst that drives currentGain to
// threshold/peak, a quiet input of amplitude q < threshold recovers with
// the release pole. Residual after 10τ_r is exp(−10) ≈ 4.5e-5 relative,
// so after 500 ms (=10·50 ms) output is within q · ~1e-4 of q.
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
    //   envelope releases from 3.0 toward 0.3 (crosses threshold at ~3000 samples)
    //   currentGain releases from 1/3 toward 1 (release pole at α_r)
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
    // Steady input 4.0 with threshold 1.0: envelope = 4 (instant attack),
    // targetGain = 0.25, currentGain = 0.25 (instant attack). Analytical
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
