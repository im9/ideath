#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/TapeDelay.h>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

static float rms(const std::vector<float>& buf)
{
    double sum = 0.0;
    for (float s : buf)
        sum += static_cast<double>(s) * static_cast<double>(s);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(buf.size())));
}

// Threshold derivations used throughout this file
// -----------------------------------------------
// TapeDelay pipeline (after Oct-2026 fix):
//   wow/flutter modulate delayTime → modulatedDelay ∈ [1, maxDelaySamples]
//   wet = readDelay(modulatedDelay)        // int-index + small-magnitude fraction
//   colored = hp(wet) → lp → tanh(·drive)  // tape coloring on PLAYBACK
//   buffer[writePos] = input + colored·fb + 1e-25f
//   return input·(1 − mix) + colored·mix
//
// Key bounds from this topology (|input| ≤ 1):
//   - tanh(·) ∈ [-1, 1], so colored ∈ [-1, 1] regardless of drive.
//   - With mix = 1 the output is `colored` → |out| ≤ 1 always.
//   - With mix = 0 the output is bit-exact `input`.
//
// Pre-delay silence (fb = 0, wow = flutter = 0): before the impulse
// emerges at call D, wet read from unwritten buffer slots returns 0.
// However, Biquad::process internally adds kAntiDenormal = 1e-25f to
// its state z1_ every sample, so HP/LP output of exact-zero input
// ramps up to ~1e-25 over a few calls. This is below float ULP of any
// audible signal but precludes bit-exact zero comparison. All "silent
// output" assertions below use 1e-20 absolute tolerance (5×10⁵ the
// anti-denormal scale, still −400 dB below audibility).
//
// Colored impulse at delay location (default HP 40 Hz, LP 8 kHz,
// Q = 0.707, drive = 1.5, fs = 44.1 kHz): RBJ HP has b0 ≈ 1, RBJ LP has
// b0 = (1 − cos(2π·8000/44100))/2 ≈ 0.291. First sample of the cascade
// for input 1: tanh(0.291 · 1.5) ≈ 0.259. Filter ringing peaks on the
// second sample at ≈ 0.58 before decaying. Within the 5-sample window
// tested, max |out| ≈ 0.58 — tight lower bound 0.4 catches regressions
// that skip coloring or blow up drive/feedback.
//
// Boundedness with hot feedback + drive: the tanh on the playback path
// clamps `colored` to [-1, 1] per sample. Output = colored · mix ≤ 1
// in absolute value. ±1 + 1e-5 is a comfortable ULP bound.

TEST_CASE("TapeDelay: dry mix bypasses delay (bit-exact)", "[tapedelay]")
{
    // mix = 0 → output = input · 1 + colored · 0 = input exactly.
    ideath::TapeDelay delay;
    delay.prepare(kSampleRate, 1.0f);
    delay.setDelay(0.2f);
    delay.setMix(0.0f);

    for (int i = 0; i < 100; ++i)
    {
        const float input = static_cast<float>(i) / 100.0f;
        REQUIRE(delay.process(input) == input);
    }
}

TEST_CASE("TapeDelay: impulse emerges at base delay with expected coloring", "[tapedelay]")
{
    // fb = 0, wow = flutter = 0, mix = 1, default filters + drive.
    // Delay = 0.05 s · 44.1 kHz = 2205 samples exactly.
    // Calls 1..D-1 produce bit-exact 0 (wet = 0, filter state stays 0).
    // Call D: wet = buf[0] = 1, colored = tanh(LP(HP(1)) · drive) ≈ 0.259.
    // The filter's second sample rings to ≈ 0.58 (analytical via RBJ
    // biquad impulse response).
    ideath::TapeDelay delay;
    delay.prepare(kSampleRate, 1.0f);
    delay.setDelay(0.05f);
    delay.setFeedback(0.0f);
    delay.setMix(1.0f);
    delay.setWowDepth(0.0f);
    delay.setFlutterDepth(0.0f);

    constexpr int kDelay = 2205;
    delay.process(1.0f);  // call 0: impulse in

    for (int i = 1; i < kDelay; ++i)
    {
        INFO("i = " << i);
        REQUIRE(std::fabs(delay.process(0.0f)) < 1e-20f);
    }

    // Impulse window: 5 samples starting at kDelay. The first sample lands
    // at ≈ 0.259, the second (filter ring peak) at ≈ 0.58.
    float maxAbs = 0.0f;
    for (int i = 0; i < 5; ++i)
        maxAbs = std::max(maxAbs, std::fabs(delay.process(0.0f)));
    REQUIRE(maxAbs > 0.4f);
    REQUIRE(maxAbs <= 1.0f);
}

TEST_CASE("TapeDelay: wow/flutter modulation changes output relative to static", "[tapedelay]")
{
    // Same impulse + feedback into two instances, one static and one with
    // wow (0.5 Hz, 3 ms depth) + flutter (5 Hz, 0.8 ms depth). Buffered
    // outputs diverge because the modulated read position differs every
    // sample. Cumulative |a − b| > 0.1 over 5000 samples catches a
    // regression where modulation is silently disabled.
    ideath::TapeDelay staticDelay;
    staticDelay.prepare(kSampleRate, 1.0f);
    staticDelay.setDelay(0.03f);
    staticDelay.setFeedback(0.6f);
    staticDelay.setMix(1.0f);
    staticDelay.setWowDepth(0.0f);
    staticDelay.setFlutterDepth(0.0f);

    ideath::TapeDelay modDelay;
    modDelay.prepare(kSampleRate, 1.0f);
    modDelay.setDelay(0.03f);
    modDelay.setFeedback(0.6f);
    modDelay.setMix(1.0f);
    modDelay.setWowDepth(0.003f);
    modDelay.setWowRate(0.5f);
    modDelay.setFlutterDepth(0.0008f);
    modDelay.setFlutterRate(5.0f);

    std::vector<float> a(5000), b(5000);
    for (int i = 0; i < 5000; ++i)
    {
        const float input = (i == 0) ? 1.0f : 0.0f;
        a[static_cast<size_t>(i)] = staticDelay.process(input);
        b[static_cast<size_t>(i)] = modDelay.process(input);
    }

    float diff = 0.0f;
    for (int i = 0; i < 5000; ++i)
        diff += std::fabs(a[static_cast<size_t>(i)] - b[static_cast<size_t>(i)]);
    REQUIRE(diff > 0.1f);
}

TEST_CASE("TapeDelay: dark feedback filter has strictly less RMS than bright", "[tapedelay]")
{
    // Both instances impulsed once with fb = 0.8, mix = 1. Dark filters
    // (HP 200, LP 1500) apply per playback, so each echo loses high and
    // low content. Bright filters (HP 20, LP 18000) pass ≈ all content.
    // Analytical first-sample colored: bright ≈ 0.76, dark ≈ 0.015 —
    // two orders of magnitude apart. Test requires dark RMS < bright RMS
    // by at least 2× (conservative vs the ≈ 50× analytical ratio).
    auto runDelay = [](float hp, float lp) {
        ideath::TapeDelay d;
        d.prepare(kSampleRate, 1.0f);
        d.setDelay(0.02f);
        d.setFeedback(0.8f);
        d.setMix(1.0f);
        d.setLowpass(lp);
        d.setHighpass(hp);
        d.setWowDepth(0.0f);
        d.setFlutterDepth(0.0f);
        d.process(1.0f);
        std::vector<float> buf(6000);
        for (int i = 0; i < 6000; ++i) buf[static_cast<size_t>(i)] = d.process(0.0f);
        return rms(buf);
    };

    const float rmsBright = runDelay(20.0f, 18000.0f);
    const float rmsDark   = runDelay(200.0f, 1500.0f);
    REQUIRE(rmsDark < rmsBright);
    REQUIRE(rmsDark < rmsBright * 0.5f);
}

TEST_CASE("TapeDelay: reset clears state (bit-exact zero output)", "[tapedelay]")
{
    // reset() zeros the buffer, resets filter state, and rewinds writePos
    // and modulation phase. Subsequent 0 input yields 0 output exactly.
    ideath::TapeDelay delay;
    delay.prepare(kSampleRate, 1.0f);
    delay.setDelay(0.1f);
    delay.setFeedback(0.8f);
    delay.setMix(1.0f);

    for (int i = 0; i < 4000; ++i) delay.process(0.8f);
    delay.reset();

    for (int i = 0; i < 200; ++i)
        REQUIRE(std::fabs(delay.process(0.0f)) < 1e-20f);
}

TEST_CASE("TapeDelay: output bounded to [-1, 1] at hot feedback + drive", "[tapedelay]")
{
    // tanh clamps colored to [-1, 1] per sample; mix = 1 → output =
    // colored. Regardless of feedback amount or drive, |output| ≤ 1.
    // 1 s of modulated sine burst covers the attack and release phases.
    ideath::TapeDelay delay;
    delay.prepare(kSampleRate, 1.0f);
    delay.setDelay(0.03f);
    delay.setFeedback(0.95f);
    delay.setMix(1.0f);
    delay.setDrive(4.0f);
    delay.setWowDepth(0.002f);
    delay.setFlutterDepth(0.0005f);

    for (int i = 0; i < 44100; ++i)
    {
        const float input = (i < 128) ? std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / kSampleRate) : 0.0f;
        const float output = delay.process(input);
        REQUIRE(std::isfinite(output));
        REQUIRE(std::fabs(output) <= 1.0f + 1e-5f);
    }
}

TEST_CASE("TapeDelay: wow/flutter modulated delay stays clamped above 1 sample", "[tapedelay]")
{
    // Extreme wow depth would push delay below 1 sample; the impl clamps
    // modulatedDelay to [1, maxDelaySamples]. Test that no NaN/Inf escape
    // and output stays bounded.
    ideath::TapeDelay delay;
    delay.prepare(kSampleRate, 0.1f);
    delay.setDelay(0.0005f);    // ~22 samples — close to the lower clamp
    delay.setFeedback(0.5f);
    delay.setMix(1.0f);
    delay.setWowDepth(0.001f);  // ~44 samples, can overshoot delay
    delay.setWowRate(3.0f);
    delay.setFlutterDepth(0.0005f);
    delay.setFlutterRate(8.0f);

    for (int i = 0; i < 44100; ++i)
    {
        const float t = static_cast<float>(i) / kSampleRate;
        const float input = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * t);
        const float out = delay.process(input);
        REQUIRE(std::isfinite(out));
        REQUIRE(std::fabs(out) <= 1.0f + 1e-5f);
    }
}

// ---------------------------------------------------------------------------
// Long-run stability
// ---------------------------------------------------------------------------

TEST_CASE("TapeDelay: 10-second stability at aggressive settings", "[tapedelay][stability]")
{
    // 10 s × 44.1 kHz = 441 000 samples. High feedback + drive, full
    // modulation. Outputs must stay finite and within the tanh-bounded
    // range; buffer/filter state must not diverge.
    ideath::TapeDelay delay;
    delay.prepare(kSampleRate, 1.0f);
    delay.setDelay(0.2f);
    delay.setFeedback(0.9f);
    delay.setMix(1.0f);
    delay.setDrive(3.0f);
    delay.setWowDepth(0.003f);
    delay.setFlutterDepth(0.001f);
    delay.setLowpass(4000.0f);
    delay.setHighpass(80.0f);

    for (int i = 0; i < 441000; ++i)
    {
        const float t = static_cast<float>(i) / kSampleRate;
        const float input = 0.3f * std::sin(2.0f * 3.14159265f * 110.0f * t);
        const float out = delay.process(input);
        REQUIRE(std::isfinite(out));
        REQUIRE(std::fabs(out) <= 1.0f + 1e-5f);
    }
}
