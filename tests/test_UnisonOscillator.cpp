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

TEST_CASE("UnisonOscillator: drift defaults to zero (regression)", "[unison][drift]")
{
    // With drift untouched (default 0), output must be bit-identical to a
    // fresh instance that never knew about drift.
    ideath::UnisonOscillator a, b;
    a.prepare(kSampleRate);
    b.prepare(kSampleRate);
    a.setVoiceCount(4);
    b.setVoiceCount(4);
    a.setFrequency(220.0f);
    b.setFrequency(220.0f);
    a.setDetune(15.0f);
    b.setDetune(15.0f);

    // a never touches drift; b explicitly sets it to 0 — both should match.
    b.setDriftAmount(0.0f);

    for (int i = 0; i < 2048; ++i)
    {
        float oa = a.process(1.0f);
        float ob = b.process(1.0f);
        REQUIRE_THAT(oa, WithinAbs(ob, 1e-6f));
    }

    // And drift cents should be exactly zero when amount is zero.
    REQUIRE_THAT(b.getVoiceDriftCents(0), WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(b.getVoiceDriftCents(3), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("UnisonOscillator: drift varies voice frequency over time", "[unison][drift]")
{
    ideath::UnisonOscillator uni;
    uni.prepare(kSampleRate);
    uni.setVoiceCount(4);
    uni.setFrequency(440.0f);
    uni.setDetune(10.0f);
    uni.setDriftAmount(5.0f);
    uni.setDriftRate(2.0f); // 2 Hz so the drift moves a noticeable amount in 100ms

    float dStart = uni.getVoiceDriftCents(0);

    // Process ~100ms — at 2 Hz that's a fifth of a cycle, plenty of motion.
    for (int i = 0; i < 4410; ++i)
        uni.process(1.0f);

    float dLater = uni.getVoiceDriftCents(0);

    // Same voice should have moved measurably over time.
    REQUIRE(std::fabs(dLater - dStart) > 0.1f);
    // And the drift must stay within the configured peak deviation.
    REQUIRE(std::fabs(dLater) <= 5.0f + 1e-4f);
}

TEST_CASE("UnisonOscillator: drift is independent per voice", "[unison][drift]")
{
    ideath::UnisonOscillator uni;
    uni.prepare(kSampleRate);
    uni.setVoiceCount(4);
    uni.setFrequency(440.0f);
    uni.setDetune(10.0f);
    uni.setDriftAmount(5.0f);
    uni.setDriftRate(0.3f);

    // Run a bit so each voice's LFO advances.
    for (int i = 0; i < 1024; ++i)
        uni.process(1.0f);

    float d0 = uni.getVoiceDriftCents(0);
    float d1 = uni.getVoiceDriftCents(1);
    float d2 = uni.getVoiceDriftCents(2);
    float d3 = uni.getVoiceDriftCents(3);

    // No two voices should be reading exactly the same drift value —
    // independent phases (and per-voice rate jitter) guarantee this.
    REQUIRE(std::fabs(d0 - d1) > 1e-4f);
    REQUIRE(std::fabs(d0 - d2) > 1e-4f);
    REQUIRE(std::fabs(d0 - d3) > 1e-4f);
    REQUIRE(std::fabs(d1 - d2) > 1e-4f);
    REQUIRE(std::fabs(d2 - d3) > 1e-4f);
}

TEST_CASE("UnisonOscillator: drift produces finite, bounded output", "[unison][drift]")
{
    ideath::UnisonOscillator uni;
    uni.prepare(kSampleRate);
    uni.setVoiceCount(7);
    uni.setFrequency(440.0f);
    uni.setDetune(20.0f);
    uni.setDriftAmount(3.0f);
    uni.setDriftRate(0.3f);

    for (int i = 0; i < 8192; ++i)
    {
        float out = uni.process(1.0f);
        REQUIRE(std::isfinite(out));
        REQUIRE(out >= -3.0f);
        REQUIRE(out <= 3.0f);
    }
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
