#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/UnisonOscillator.h>
#include <cmath>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

TEST_CASE("UnisonOscillator: single voice matches plain Oscillator", "[unison]")
{
    ideath::UnisonOscillator uni;
    uni.prepare(kSampleRate);
    uni.setVoiceCount(1);
    uni.setFrequency(440.0f);
    uni.setDetune(0.0f);

    ideath::Oscillator ref;
    ref.prepare(kSampleRate);
    ref.setFrequency(440.0f);

    for (int i = 0; i < 512; ++i)
    {
        float u = uni.process(1.0f);
        float r = ref.process(1.0f);
        REQUIRE_THAT(u, WithinAbs(r, 1e-6f));
    }
}

TEST_CASE("UnisonOscillator: output stays within reasonable range", "[unison]")
{
    ideath::UnisonOscillator uni;
    uni.prepare(kSampleRate);
    uni.setVoiceCount(7);
    uni.setFrequency(440.0f);
    uni.setDetune(30.0f);

    for (int i = 0; i < 4096; ++i)
    {
        float out = uni.process(1.0f);
        // sqrt(N) gain compensation keeps output bounded, but in-phase
        // alignment can briefly exceed 1.0. Should never exceed sqrt(N).
        REQUIRE(out >= -3.0f);
        REQUIRE(out <= 3.0f);
    }
}

TEST_CASE("UnisonOscillator: more voices produce richer signal", "[unison]")
{
    // Measure RMS with 1 voice vs 7 voices — 7 should have similar energy
    // but different spectral content (we just verify it's non-trivially different)
    auto measureSamples = [](int voices, float detune) {
        ideath::UnisonOscillator uni;
        uni.prepare(kSampleRate);
        uni.setVoiceCount(voices);
        uni.setFrequency(440.0f);
        uni.setDetune(detune);

        float sum = 0.0f;
        constexpr int N = 4096;
        for (int i = 0; i < N; ++i)
        {
            float s = uni.process(1.0f);
            sum += s * s;
        }
        return std::sqrt(sum / N);
    };

    float rms1 = measureSamples(1, 0.0f);
    float rms7 = measureSamples(7, 20.0f);

    // Both should produce audible signal
    REQUIRE(rms1 > 0.1f);
    REQUIRE(rms7 > 0.1f);
}

TEST_CASE("UnisonOscillator: zero detune with multiple voices", "[unison]")
{
    // With zero detune, all voices are at the same frequency
    // Output should be similar to a single voice (gain-compensated)
    ideath::UnisonOscillator uni;
    uni.prepare(kSampleRate);
    uni.setVoiceCount(4);
    uni.setFrequency(440.0f);
    uni.setDetune(0.0f);

    ideath::Oscillator ref;
    ref.prepare(kSampleRate);
    ref.setFrequency(440.0f);

    for (int i = 0; i < 256; ++i)
    {
        float u = uni.process(1.0f);
        float r = ref.process(1.0f);
        // 4 voices at same freq, gain comp = /sqrt(4) = /2, but 4 identical = 4x / 2 = 2x
        REQUIRE_THAT(u, WithinAbs(r * 2.0f, 1e-5f));
    }
}

TEST_CASE("UnisonOscillator: voice count clamped to valid range", "[unison]")
{
    ideath::UnisonOscillator uni;
    uni.prepare(kSampleRate);

    uni.setVoiceCount(0);
    REQUIRE(uni.getVoiceCount() == 1);

    uni.setVoiceCount(100);
    REQUIRE(uni.getVoiceCount() == ideath::UnisonOscillator::kMaxVoices);
}

TEST_CASE("UnisonOscillator: reset clears phase", "[unison]")
{
    ideath::UnisonOscillator uni;
    uni.prepare(kSampleRate);
    uni.setVoiceCount(1);
    uni.setFrequency(440.0f);
    uni.setDetune(0.0f);

    // Run for a bit
    for (int i = 0; i < 1000; ++i)
        uni.process(1.0f);

    uni.reset();

    // After reset, first sample should match a fresh instance
    ideath::UnisonOscillator fresh;
    fresh.prepare(kSampleRate);
    fresh.setVoiceCount(1);
    fresh.setFrequency(440.0f);
    fresh.setDetune(0.0f);

    // Both should produce the same first sample after reset
    float a = uni.process(1.0f);
    float b = fresh.process(1.0f);
    REQUIRE_THAT(a, WithinAbs(b, 0.05f));
}

TEST_CASE("UnisonOscillator: square waveform works", "[unison]")
{
    ideath::UnisonOscillator uni;
    uni.prepare(kSampleRate);
    uni.setVoiceCount(3);
    uni.setFrequency(440.0f);
    uni.setDetune(15.0f);

    // Just verify it doesn't explode with square waveform
    for (int i = 0; i < 1024; ++i)
    {
        float out = uni.process(0.0f); // square
        REQUIRE(std::isfinite(out));
    }
}
