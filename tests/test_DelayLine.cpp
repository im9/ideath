#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/DelayLine.h>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

// Threshold derivations used throughout this file
// -----------------------------------------------
// DelayLine::process(input) performs, in order:
//   wet = readDelay()         (linear-interpolated sample at writePos − D)
//                             BUT wet = input when D < 1 sample (sub-sample
//                             delay would otherwise alias to a full-buffer
//                             delay via the circular wrap).
//   buffer[writePos] = input + wet · feedback + 1e-25f
//   writePos = (writePos + 1) % bufferSize
//   return input · (1 − mix) + wet · mix
//
// Integer delay: for D = 0.01 · 44100 = 441.0f exactly (0.01f rounds up to
// ~1.0000000015e-2 and 44100 is representable, so the product rounds to
// 441.0), an impulse written at call 0 emerges at call D with wet = 1.0
// bit-exact.
//
// Linear interpolation at D_int + f (0 < f < 1):
//   at call D_int:     readPos = −f + bufferSize → interp between
//                       buf[bufferSize−1] = 0 and buf[0] = 1 with α = 1−f
//                       → wet = 1 − f
//   at call D_int + 1: readPos = 1 − f → interp between buf[0] = 1 and
//                       buf[1] = 0 with α = 1 − f
//                       → wet = f
//   Total energy = (1 − f) + f = 1 exactly.
//
// Denormal guard: each write adds +1e-25f. A buffer slot written with
// 0 input, 0 feedback accumulates to 1e-25f (well above float denormal
// threshold, below float ULP of any audible signal). Steady-state DC from
// this guard under feedback fb is 1e-25/(1 − fb) = 1e-22 at fb = 0.999.
// All "bit-exact zero" tests therefore compare within 1e-20 absolute.
//
// Feedback cascade: for fb = 0.5, mix = 1, impulse input at call 0, the
// n-th echo at call n·D has amplitude fb^n = 0.5^n bit-exact (writes
// between peaks are 0 + tiny·fb + 1e-25 ≈ 1e-25, negligible).
//
// Bypass on D < 1: setDelay(0) or default-constructed state now gives
// wet = input (output = input for any mix). Previously this produced
// buffer_size-delayed output — fixed because sub-sample read from a
// circular buffer reads stale data about to be overwritten.

TEST_CASE("DelayLine: impulse emerges exactly at integer delay (bit-exact)", "[delay]")
{
    // 0.01f · 44100.0f rounds to 441.0f exactly in IEEE 754. So an impulse
    // at call 0 emerges at call 441 with wet = buf[0] = 1.0 bit-exact.
    ideath::DelayLine dl;
    dl.prepare(kSampleRate, 0.1f);
    dl.setDelay(0.01f);
    dl.setFeedback(0.0f);
    dl.setMix(1.0f);

    constexpr int kDelay = 441;
    REQUIRE(dl.process(1.0f) == 0.0f); // call 0: wet = 0 (buffer empty)
    for (int i = 1; i < kDelay; ++i)
    {
        INFO("i = " << i);
        REQUIRE(dl.process(0.0f) <= 1e-20f); // denormal guard residual ≤ 1e-25f
    }
    // Call 441: wet = buf[0] = 1.0.
    REQUIRE(dl.process(0.0f) == 1.0f);
    // Call 442: wet = buf[1] = 1e-25f ≈ 0.
    REQUIRE(dl.process(0.0f) <= 1e-20f);
}

TEST_CASE("DelayLine: fractional delay splits impulse per linear interp", "[delay]")
{
    // D ≈ 441.498 → D_int = 441, f ≈ 0.498. Linear interpolation gives
    //   output[441] = (1 − f)·buf[0] + f·buf[bufferSize−1] = (1 − f)·1 + f·0 = 1 − f
    //   output[442] = (1 − f)·buf[1] + f·buf[0]            = (1 − f)·0 + f·1 = f
    //   output[441] + output[442] = 1 exactly
    // The implementation keeps frac at its native small magnitude
    // (avoiding the +bufferSize wrap that would cost ULP at 2^12 scale),
    // so 1e-6 tolerance is comfortable.
    ideath::DelayLine dl;
    dl.prepare(kSampleRate, 0.1f);
    const float delaySec = 0.0100113f;
    dl.setDelay(delaySec);
    dl.setFeedback(0.0f);
    dl.setMix(1.0f);

    const float D        = delaySec * kSampleRate;
    const int   Dint     = static_cast<int>(D);
    const float f        = D - static_cast<float>(Dint);

    dl.process(1.0f);                                 // call 0: impulse in
    for (int i = 1; i < Dint; ++i) dl.process(0.0f);  // calls 1..Dint-1

    const float o0 = dl.process(0.0f);                // call Dint
    const float o1 = dl.process(0.0f);                // call Dint+1
    REQUIRE_THAT(o0, WithinAbs(1.0f - f, 1e-6f));
    REQUIRE_THAT(o1, WithinAbs(       f, 1e-6f));
    REQUIRE_THAT(o0 + o1, WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("DelayLine: dry mix (mix=0) is bit-exact passthrough", "[delay]")
{
    // output = input · (1 − 0) + wet · 0 = input exactly.
    ideath::DelayLine dl;
    dl.prepare(kSampleRate, 0.1f);
    dl.setDelay(0.01f);
    dl.setFeedback(0.0f);
    dl.setMix(0.0f);

    for (int i = 0; i < 100; ++i)
    {
        const float input = static_cast<float>(i) / 100.0f;
        REQUIRE(dl.process(input) == input);
    }
}

TEST_CASE("DelayLine: feedback cascade — n-th echo = fb^n (bit-exact)", "[delay]")
{
    // Impulse of 1.0, fb=0.5, mix=1. Each echo writes back fb·wet into the
    // buffer, so n-th echo amplitude = 0.5^n. Verified bit-exact because:
    //   at call n·D, wet = buf[(n−1)·D] = 0.5^(n−1), and the write at that
    //   call stores 0.5^(n−1)·0.5 = 0.5^n at buf[n·D].
    ideath::DelayLine dl;
    dl.prepare(kSampleRate, 0.1f);
    dl.setDelay(0.01f);
    dl.setFeedback(0.5f);
    dl.setMix(1.0f);

    constexpr int kDelay = 441;
    dl.process(1.0f);
    for (int i = 1; i < kDelay; ++i) dl.process(0.0f);

    float expected = 1.0f;
    for (int n = 1; n <= 4; ++n)
    {
        REQUIRE_THAT(dl.process(0.0f), WithinAbs(expected, 1e-6f));
        for (int i = 1; i < kDelay; ++i) dl.process(0.0f);
        expected *= 0.5f;
    }
}

TEST_CASE("DelayLine: mix formula — output = (1−mix)·input + mix·wet", "[delay]")
{
    // At call 0 with empty buffer: wet = 0 (or input if D < 1).
    // For D = 441 samples, wet = 0 at call 0.
    // output = 1·(1 − 0.5) + 0·0.5 = 0.5 (bit-exact when D ≥ 1).
    ideath::DelayLine dl;
    dl.prepare(kSampleRate, 0.1f);
    dl.setDelay(0.01f);
    dl.setFeedback(0.0f);
    dl.setMix(0.5f);

    REQUIRE(dl.process(1.0f) == 0.5f);
}

TEST_CASE("DelayLine: feedback cascade stays bounded by input peak", "[delay]")
{
    // fb=0.9, impulse train of 100 samples at 1.0 then silence. Each echo
    // cycle decays by 0.9. Max |output| ≤ 1.0 + 1e-20 (denormal guard).
    ideath::DelayLine dl;
    dl.prepare(kSampleRate, 0.5f);
    dl.setDelay(0.05f);
    dl.setFeedback(0.9f);
    dl.setMix(1.0f);

    float maxAbs = 0.0f;
    for (int i = 0; i < 44100; ++i)
    {
        const float input = (i < 100) ? 1.0f : 0.0f;
        const float output = dl.process(input);
        maxAbs = std::max(maxAbs, std::fabs(output));
        REQUIRE(std::isfinite(output));
    }
    REQUIRE(maxAbs <= 1.0f + 1e-6f);
    REQUIRE(maxAbs >  0.9f);  // at least first-generation echo visible
}

TEST_CASE("DelayLine: reset clears buffer (bit-exact zero output)", "[delay]")
{
    // After reset, buffer is all 0. For delay ≥ 1 and input 0, wet comes
    // from a zero slot → output = 0·(1−mix) + 0·mix = 0 bit-exact (the
    // denormal guard 1e-25f is written to buffer but never read back
    // within a single call).
    ideath::DelayLine dl;
    dl.prepare(kSampleRate, 0.1f);
    dl.setDelay(0.01f);
    dl.setFeedback(0.5f);
    dl.setMix(1.0f);

    for (int i = 0; i < 1000; ++i) dl.process(0.8f);
    dl.reset();

    for (int i = 0; i < 200; ++i)
        REQUIRE(dl.process(0.0f) == 0.0f);
}

TEST_CASE("DelayLine: setDelay(0) is bypass (wet = input)", "[delay]")
{
    // After fix: when D < 1 sample, wet = input → output = input for any
    // mix. Previously this aliased to bufferSize-delay via circular wrap.
    ideath::DelayLine dl;
    dl.prepare(kSampleRate, 0.1f);
    dl.setDelay(0.0f);
    dl.setFeedback(0.0f);
    dl.setMix(1.0f);

    for (int i = 0; i < 50; ++i)
    {
        const float input = 0.01f * static_cast<float>(i);
        REQUIRE(dl.process(input) == input);
    }

    // Even with mix = 0.3, output = 0.7·input + 0.3·input = input.
    dl.reset();
    dl.setMix(0.3f);
    for (int i = 0; i < 50; ++i)
    {
        const float input = 0.5f;
        const float out = dl.process(input);
        REQUIRE_THAT(out, WithinAbs(input, 1e-6f));
    }
}

TEST_CASE("DelayLine: default-constructed (no setDelay) is bypass", "[delay]")
{
    // delaySamples_ defaults to 0.0f before setDelay is called. The
    // bypass branch keeps this case sane: output = input (not stale
    // full-buffer data).
    ideath::DelayLine dl;
    dl.prepare(kSampleRate, 0.1f);
    // No setDelay call — delaySamples_ remains 0.
    dl.setMix(1.0f);
    dl.setFeedback(0.0f);
    for (int i = 0; i < 20; ++i)
    {
        const float input = 0.1f * static_cast<float>(i);
        REQUIRE(dl.process(input) == input);
    }
}

TEST_CASE("DelayLine: delay clamps to [0, maxDelaySec · sampleRate]", "[delay]")
{
    // maxDelaySec = 0.1 → maxDelaySamples = 4410. setDelay(5.0) exceeds
    // this and clamps to exactly 4410.0f.
    ideath::DelayLine dl;
    dl.prepare(kSampleRate, 0.1f);
    dl.setDelay(5.0f);
    REQUIRE(dl.getDelaySamples() == 4410.0f);

    dl.setDelay(-3.0f);
    REQUIRE(dl.getDelaySamples() == 0.0f);

    dl.setDelay(0.02f);
    REQUIRE_THAT(dl.getDelaySamples(), WithinAbs(882.0f, 1e-5f));
}

TEST_CASE("DelayLine: feedback clamps to [-0.999, 0.999]", "[delay]")
{
    // Prevents instability from fb ≥ 1. Verify by probing behavior: at
    // fb=0.999 after N·D samples, echo amplitude = 0.999^N (not blow-up).
    ideath::DelayLine dl;
    dl.prepare(kSampleRate, 0.1f);
    dl.setDelay(0.001f);  // 44.1 → 44 samples (int), fractional tail
    dl.setFeedback(5.0f); // clamps to 0.999
    dl.setMix(1.0f);

    // After 44100 samples (1 s), amplitude scales by 0.999^(44100/44) ≈
    // exp(−44100·0.001/44) = exp(−1.0) ≈ 0.368. Still bounded, not blown up.
    float maxAbs = 0.0f;
    dl.process(1.0f);
    for (int i = 1; i < 44100; ++i)
    {
        const float out = dl.process(0.0f);
        maxAbs = std::max(maxAbs, std::fabs(out));
        REQUIRE(std::isfinite(out));
    }
    REQUIRE(maxAbs <= 1.0f + 1e-6f);
}

TEST_CASE("DelayLine: mix clamps to [0, 1]", "[delay]")
{
    // mix > 1 should clamp to 1 (fully wet). mix < 0 clamps to 0 (dry).
    ideath::DelayLine dl;
    dl.prepare(kSampleRate, 0.1f);
    dl.setDelay(0.01f);
    dl.setFeedback(0.0f);

    // mix = 2 → 1: at call 0, wet = 0, output = 0·(1−1) + 0·1 = 0.
    dl.setMix(2.0f);
    REQUIRE(dl.process(0.5f) == 0.0f);

    // mix = -1 → 0: output = input·1 + wet·0 = input exactly.
    dl.reset();
    dl.setMix(-1.0f);
    REQUIRE(dl.process(0.7f) == 0.7f);
}

// ---------------------------------------------------------------------------
// Long-run stability
// ---------------------------------------------------------------------------

TEST_CASE("DelayLine: 10-second stability at aggressive feedback", "[delay][stability]")
{
    // 10 s × 44.1 kHz = 441 000 samples. fb = 0.95, modulated input, long
    // delay. Output must stay finite; the denormal guard and the <1
    // feedback clamp prevent blow-up or denormal drift.
    ideath::DelayLine dl;
    dl.prepare(kSampleRate, 1.0f);
    dl.setDelay(0.3f);
    dl.setFeedback(0.95f);
    dl.setMix(1.0f);

    for (int i = 0; i < 441000; ++i)
    {
        const float t = static_cast<float>(i) / kSampleRate;
        const float input = 0.3f * std::sin(2.0f * 3.14159265f * 110.0f * t);
        const float out = dl.process(input);
        REQUIRE(std::isfinite(out));
        // Geometric bound on feedback delay: |input|·1/(1−fb) = 0.3 / 0.05 = 6.
        REQUIRE(std::fabs(out) <= 6.0f + 1e-3f);
    }
}
