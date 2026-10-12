#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Oscillator.h>
#include <cmath>
#include <vector>
#include <numeric>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

// Helper: compute RMS of a buffer.
static float rms(const float* buf, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += static_cast<double>(buf[i]) * static_cast<double>(buf[i]);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(n)));
}

TEST_CASE("Oscillator: output is in [-1, 1] range", "[osc]")
{
    ideath::Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(440.0f);

    constexpr int N = 44100; // 1 second
    for (int i = 0; i < N; ++i)
    {
        float s = osc.process(1.0f); // saw
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
    }

    osc.reset();
    osc.setFrequency(440.0f);
    for (int i = 0; i < N; ++i)
    {
        float s = osc.process(0.0f); // square
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
    }
}

TEST_CASE("Oscillator: saw has correct frequency (zero crossings)", "[osc]")
{
    ideath::Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(440.0f);

    constexpr int N = 44100;
    int zeroCrossings = 0;
    float prev = osc.process(1.0f);
    for (int i = 1; i < N; ++i)
    {
        float s = osc.process(1.0f);
        if ((prev >= 0.0f && s < 0.0f) || (prev < 0.0f && s >= 0.0f))
            ++zeroCrossings;
        prev = s;
    }

    // Saw ramps from -1 to +1: one zero crossing at mid-ramp (phase=0.5),
    // plus one at the PolyBLEP-smoothed wrap (phase≈0, descends through zero).
    // 2 crossings/cycle × 440 Hz = 880/sec. Allow ±5%.
    REQUIRE(zeroCrossings >= 836);
    REQUIRE(zeroCrossings <= 924);
}

TEST_CASE("Oscillator: produces nonzero output", "[osc]")
{
    ideath::Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(440.0f);

    constexpr int N = 4096;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i)
        buf[static_cast<size_t>(i)] = osc.process(0.5f);

    float level = rms(buf.data(), N);
    REQUIRE(level > 0.1f);
}

TEST_CASE("Oscillator: square wave RMS is ~1.0", "[osc]")
{
    ideath::Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(440.0f);

    // Skip transient.
    for (int i = 0; i < 1000; ++i)
        osc.process(0.0f);

    constexpr int N = 44100;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i)
        buf[static_cast<size_t>(i)] = osc.process(0.0f);

    float level = rms(buf.data(), N);
    // Perfect square wave RMS = 1.0.
    REQUIRE_THAT(level, WithinAbs(1.0f, 0.05f));
}

TEST_CASE("Oscillator: PolyBLEP reduces aliasing at high frequencies", "[osc]")
{
    // Generate a high-frequency saw and measure energy above Nyquist/2.
    // PolyBLEP should reduce high-frequency energy vs naive saw.
    // We use a simple proxy: count how many sample-to-sample jumps exceed
    // a threshold. Naive saw has a ~2.0 jump at phase reset; PolyBLEP smooths it.
    ideath::Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(8000.0f); // high frequency, near Nyquist/5

    constexpr int N = 44100;
    float prev = osc.process(1.0f); // saw
    int largeJumps = 0;
    for (int i = 1; i < N; ++i)
    {
        float s = osc.process(1.0f);
        if (std::fabs(s - prev) > 1.5f) // naive saw jumps ~2.0
            ++largeJumps;
        prev = s;
    }

    // With PolyBLEP, there should be zero jumps > 1.5
    // (naive saw at 8kHz would have ~8000 such jumps per second)
    REQUIRE(largeJumps == 0);
}

TEST_CASE("Oscillator: PolyBLEP square wave has no harsh transitions", "[osc]")
{
    ideath::Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(5000.0f);

    constexpr int N = 44100;
    float prev = osc.process(0.0f); // square
    float maxJump = 0.0f;
    for (int i = 1; i < N; ++i)
    {
        float s = osc.process(0.0f);
        float jump = std::fabs(s - prev);
        if (jump > maxJump)
            maxJump = jump;
        prev = s;
    }

    // Naive square at 5kHz has jumps of 2.0.
    // PolyBLEP softens transitions; max jump should be well under 2.0.
    REQUIRE(maxJump < 1.5f);
}

TEST_CASE("Oscillator: reset returns phase to zero", "[osc]")
{
    ideath::Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(1000.0f);

    for (int i = 0; i < 500; ++i)
        osc.process(1.0f);

    REQUIRE(osc.getPhase() > 0.0f);
    osc.reset();
    REQUIRE_THAT(osc.getPhase(), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("Oscillator: nudgePhase advances phase by delta and wraps modulo 1",
          "[osc]")
{
    // nudgePhase is the hook for external phase coupling (Kuramoto-style
    // audio-rate alignment driven by a consensus signal computed in the
    // plugin layer).  Positive delta advances, negative retards; the sum
    // wraps modulo 1 per ideath "phase accumulators must wrap via
    // phase -= floor(phase)" convention.
    ideath::Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(440.0f);

    // Advance to a known phase (0.3 should match a specific
    // sample-count close enough; just read it after some processing).
    for (int i = 0; i < 100; ++i) osc.process(1.0f);
    const float before = osc.getPhase();

    // +0.25 shifts phase a quarter cycle forward.
    osc.nudgePhase(0.25f);
    const float after = osc.getPhase();
    const float expected = before + 0.25f - std::floor(before + 0.25f);
    REQUIRE_THAT(after, WithinAbs(expected, 1e-6f));
    REQUIRE(after >= 0.0f);
    REQUIRE(after < 1.0f);

    // Negative wraps up: start at ~0.1, nudge by -0.3 → wraps to 0.8.
    ideath::Oscillator osc2;
    osc2.prepare(kSampleRate);
    osc2.setFrequency(100.0f);
    // Advance until phase lands near 0.1 (~ 44 samples at 100 Hz, 44100 sr).
    for (int i = 0; i < 44; ++i) osc2.process(1.0f);
    const float b2 = osc2.getPhase();
    osc2.nudgePhase(-0.3f);
    const float a2 = osc2.getPhase();
    REQUIRE(a2 >= 0.0f);
    REQUIRE(a2 < 1.0f);
    // Expected: (b2 - 0.3) mod 1 (positive)
    float raw = b2 - 0.3f;
    const float expWrap = raw - std::floor(raw);
    REQUIRE_THAT(a2, WithinAbs(expWrap, 1e-6f));
}

TEST_CASE("Oscillator: reset preserves frequency", "[osc]")
{
    // reset() clears runtime state (phase) but must preserve parameter-derived
    // state (phaseInc). This matches Biquad/SVFilter/ADSR convention: reset
    // returns to zero-phase, not to unconfigured.
    ideath::Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(440.0f);

    for (int i = 0; i < 500; ++i)
        osc.process(1.0f);

    osc.reset();

    // After reset, output should match a fresh oscillator at the same frequency.
    // If phaseInc is preserved, the first sample advances phase by 440/44100.
    ideath::Oscillator fresh;
    fresh.prepare(kSampleRate);
    fresh.setFrequency(440.0f);

    for (int i = 0; i < 16; ++i)
    {
        float a = osc.process(1.0f);
        float b = fresh.process(1.0f);
        REQUIRE_THAT(a, WithinAbs(b, 1e-6f));
    }
}
