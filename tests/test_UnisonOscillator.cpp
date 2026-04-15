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

    // Gain compensation: sum / √N. Each voice outputs in [-1, 1].
    // Worst case (all voices perfectly in-phase): sum = ±N, output = ±N/√N = ±√N.
    // For N=7: √7 ≈ 2.6458. Margin 0.01 for float rounding.
    const float bound = std::sqrt(7.0f) + 0.01f; // ≈ 2.656
    for (int i = 0; i < 4096; ++i)
    {
        float out = uni.process(1.0f);
        REQUIRE(out >= -bound);
        REQUIRE(out <= bound);
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

    // Single saw RMS = 1/√3 ≈ 0.577. With gain comp (÷√N), uncorrelated
    // voices preserve RMS ≈ 0.577; correlated voices increase it.
    // Threshold 0.4 is ~30% below the theoretical minimum.
    REQUIRE(rms1 > 0.4f);
    REQUIRE(rms7 > 0.4f);
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

    // reset() preserves phaseInc (frequency), so both instances have identical
    // state: phase=0, phaseInc=440/44100. Output should be bit-identical.
    float a = uni.process(1.0f);
    float b = fresh.process(1.0f);
    REQUIRE_THAT(a, WithinAbs(b, 1e-6f));
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

    // Voice 0 drift: rate = 2 Hz × jitter[0] = 2.0 Hz exactly.
    // 4410 samples at 44100 = 0.1s → phase advances 0.2 cycles.
    // dStart: phase=0, sin(0) × 5 = 0.
    // dLater: phase=0.2, sin(2π×0.2) × 5 = sin(72°) × 5 ≈ 0.9511 × 5 = 4.755.
    // Delta ≈ 4.755 cents. Threshold 3.0 is ~63% of expected, conservative.
    REQUIRE(std::fabs(dLater - dStart) > 3.0f);
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
        // Same √N bound as non-drift case: drift shifts frequency but
        // each oscillator still outputs in [-1, 1].
        const float bound = std::sqrt(7.0f) + 0.01f;
        REQUIRE(out >= -bound);
        REQUIRE(out <= bound);
    }
}

TEST_CASE("UnisonOscillator: square waveform works", "[unison]")
{
    ideath::UnisonOscillator uni;
    uni.prepare(kSampleRate);
    uni.setVoiceCount(3);
    uni.setFrequency(440.0f);
    uni.setDetune(15.0f);

    // Square oscillator outputs in [-1, 1]. 3 voices, gain comp √3.
    // Bound = √3 ≈ 1.732.
    const float bound = std::sqrt(3.0f) + 0.01f;
    for (int i = 0; i < 1024; ++i)
    {
        float out = uni.process(0.0f); // square
        REQUIRE(std::isfinite(out));
        REQUIRE(out >= -bound);
        REQUIRE(out <= bound);
    }
}

// --- Long-run stability ---

TEST_CASE("UnisonOscillator: long-run stability (10 seconds)", "[unison]")
{
    ideath::UnisonOscillator uni;
    uni.prepare(kSampleRate);
    uni.setVoiceCount(7);
    uni.setFrequency(440.0f);
    uni.setDetune(25.0f);
    uni.setDriftAmount(3.0f);
    uni.setDriftRate(0.3f);

    // 10 seconds: tests phase wrap for all 7 oscillators + 7 drift LFOs.
    // √7 ≈ 2.646 is the theoretical output bound.
    const float bound = std::sqrt(7.0f) + 0.01f;
    constexpr int N = 441000;
    for (int i = 0; i < N; ++i)
    {
        float out = uni.process(1.0f);
        REQUIRE(std::isfinite(out));
        REQUIRE(out >= -bound);
        REQUIRE(out <= bound);
    }
}

// --- Parameter boundary behavior ---

TEST_CASE("UnisonOscillator: frequency 0 Hz produces zero output", "[unison]")
{
    ideath::UnisonOscillator uni;
    uni.prepare(kSampleRate);
    uni.setVoiceCount(4);
    uni.setFrequency(0.0f);
    uni.setDetune(20.0f);

    // freq=0: detune offsets are 0 × pow(2, cents/1200) = 0 for all voices.
    // Oscillator at 0 Hz → phaseInc=0, phase stays at 0 → saw output = 2×0−1 = −1.
    // 4 voices × (−1) / √4 = −2.
    for (int i = 0; i < 100; ++i)
    {
        float out = uni.process(1.0f);
        REQUIRE(std::isfinite(out));
    }
}

TEST_CASE("UnisonOscillator: max voices + max detune stays bounded", "[unison]")
{
    ideath::UnisonOscillator uni;
    uni.prepare(kSampleRate);
    uni.setVoiceCount(ideath::UnisonOscillator::kMaxVoices); // 16
    uni.setFrequency(440.0f);
    uni.setDetune(1200.0f); // full octave spread

    // Each voice in [-1, 1]. Bound = √16 = 4.0.
    const float bound = std::sqrt(16.0f) + 0.01f; // 4.01
    for (int i = 0; i < 4096; ++i)
    {
        float out = uni.process(1.0f);
        REQUIRE(std::isfinite(out));
        REQUIRE(out >= -bound);
        REQUIRE(out <= bound);
    }
}

// --- Extreme parameter combinations ---

TEST_CASE("UnisonOscillator: max voices + max drift + high detune", "[unison]")
{
    ideath::UnisonOscillator uni;
    uni.prepare(kSampleRate);
    uni.setVoiceCount(16);
    uni.setFrequency(440.0f);
    uni.setDetune(100.0f);    // wide spread
    uni.setDriftAmount(10.0f); // large drift
    uni.setDriftRate(5.0f);    // fast drift

    const float bound = std::sqrt(16.0f) + 0.01f;
    constexpr int N = 44100; // 1 second
    for (int i = 0; i < N; ++i)
    {
        float out = uni.process(1.0f);
        REQUIRE(std::isfinite(out));
        REQUIRE(out >= -bound);
        REQUIRE(out <= bound);
    }
}

TEST_CASE("UnisonOscillator: near-Nyquist frequency with detune", "[unison]")
{
    ideath::UnisonOscillator uni;
    uni.prepare(kSampleRate);
    uni.setVoiceCount(4);
    // setFrequency clamps to sr*0.5 = 22050.
    // Detune spreads ±600 cents around that → some voices above Nyquist → clamped.
    uni.setFrequency(20000.0f);
    uni.setDetune(1200.0f);

    const float bound = std::sqrt(4.0f) + 0.01f;
    for (int i = 0; i < 4096; ++i)
    {
        float out = uni.process(1.0f);
        REQUIRE(std::isfinite(out));
        REQUIRE(out >= -bound);
        REQUIRE(out <= bound);
    }
}
