#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/CombFilter.h>
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

TEST_CASE("CombFilter: impulse appears at correct delay", "[comb]")
{
    ideath::CombFilter comb;
    comb.prepare(kSampleRate, 0.1f);
    comb.setDelay(0.01f);
    comb.setFeedback(0.0f);
    comb.setMix(1.0f);

    const int delaySamples = static_cast<int>(0.01f * kSampleRate);
    comb.process(1.0f);

    // Before the echo: buffer is zeroed, wet = 0. kAntiDenormal (1e-25)
    // is only written to the current position, not read here. Output = 0.
    for (int i = 1; i < delaySamples - 1; ++i)
        REQUIRE_THAT(comb.process(0.0f), WithinAbs(0.0f, 1e-6f));

    // Impulse readback: delay = 0.01s × 44100 = 441 samples (integer).
    // buffer[0] = 1.0 + 0 + 1e-25 ≈ 1.0. With mix=1: output = wet = 1.0.
    bool foundImpulse = false;
    for (int i = 0; i < 3; ++i)
    {
        if (std::fabs(comb.process(0.0f)) > 0.9f)
            foundImpulse = true;
    }
    REQUIRE(foundImpulse);
}

TEST_CASE("CombFilter: feedback produces decaying repeats", "[comb]")
{
    ideath::CombFilter comb;
    comb.prepare(kSampleRate, 0.1f);
    comb.setDelay(0.01f);
    comb.setFeedback(0.8f);
    comb.setDamp(0.2f);
    comb.setMix(1.0f);

    const int delaySamples = static_cast<int>(0.01f * kSampleRate);
    comb.process(1.0f);

    for (int i = 1; i < delaySamples; ++i)
        comb.process(0.0f);

    float echo1 = 0.0f;
    for (int i = 0; i < 3; ++i)
        echo1 = std::max(echo1, std::fabs(comb.process(0.0f)));

    for (int i = 0; i < delaySamples - 3; ++i)
        comb.process(0.0f);

    float echo2 = 0.0f;
    for (int i = 0; i < 3; ++i)
        echo2 = std::max(echo2, std::fabs(comb.process(0.0f)));

    // echo1: first readback of impulse (1.0) through delay, output = wet = 1.0.
    // echo2: second readback. buffer was written as 0 + filterStore × 0.8.
    //   filterStore = 0 + (1.0 − 0) × (1−0.2) = 0.8. buffer = 0.8 × 0.8 = 0.64.
    //   Between echoes, filterStore decays to ~0 (440 samples of wet≈0).
    //   echo2 output = wet = 0.64. Thresholds at 80% of expected values.
    REQUIRE(echo1 > 0.8f);
    REQUIRE(echo2 > 0.4f);
    REQUIRE(echo2 < echo1);
}

TEST_CASE("CombFilter: damp darkens the tail", "[comb]")
{
    ideath::CombFilter bright;
    bright.prepare(kSampleRate, 0.05f);
    bright.setDelay(0.005f);
    bright.setFeedback(0.95f);
    bright.setDamp(0.0f);
    bright.setMix(1.0f);

    ideath::CombFilter dark;
    dark.prepare(kSampleRate, 0.05f);
    dark.setDelay(0.005f);
    dark.setFeedback(0.95f);
    dark.setDamp(0.9f);
    dark.setMix(1.0f);

    bright.process(1.0f);
    dark.process(1.0f);

    std::vector<float> bufBright(4000), bufDark(4000);
    for (int i = 0; i < 4000; ++i)
    {
        bufBright[static_cast<size_t>(i)] = bright.process(0.0f);
        bufDark[static_cast<size_t>(i)] = dark.process(0.0f);
    }

    REQUIRE(rms(bufDark) < rms(bufBright));
}

TEST_CASE("CombFilter: reset clears state", "[comb]")
{
    ideath::CombFilter comb;
    comb.prepare(kSampleRate, 0.1f);
    comb.setDelay(0.01f);
    comb.setFeedback(0.9f);
    comb.setMix(1.0f);

    for (int i = 0; i < 1000; ++i)
        comb.process(0.8f);

    comb.reset();
    // After reset, buffer is zeroed and filterStore=0.
    // process(0) reads from zeroed buffer → wet=0. Output = 0×(1-1)+0×1 = 0.
    REQUIRE_THAT(comb.process(0.0f), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("CombFilter: output stays bounded", "[comb]")
{
    ideath::CombFilter comb;
    comb.prepare(kSampleRate, 0.1f);
    comb.setDelay(0.002f);
    comb.setFeedback(0.99f);
    comb.setDamp(0.1f);
    comb.setMix(1.0f);

    for (int i = 0; i < 44100; ++i)
    {
        float input = (i < 32) ? 1.0f : 0.0f;
        float output = comb.process(input);
        REQUIRE(std::isfinite(output));
        // mix=1 → output = wet. Feedback=0.99 (clamped to 0.999), damp=0.1.
        // 32-sample burst into 88-sample delay: burst < delay, so no
        // recirculation build-up during burst. Peak echo = 1.0 (burst level).
        // Subsequent echoes decay at ~0.999 × damp_filter per tap.
        REQUIRE(output >= -1.5f);
        REQUIRE(output <= 1.5f);
    }
}

TEST_CASE("CombFilter: delay is clamped to max", "[comb]")
{
    ideath::CombFilter comb;
    comb.prepare(kSampleRate, 0.05f);
    // maxDelaySamples = 0.05 × 44100 = 2205.0. setDelay(1.0) → 1.0 × 44100
    // = 44100 → clamped to 2205. +1 margin for rounding.
    comb.setDelay(1.0f);
    REQUIRE(comb.getDelaySamples() <= 0.05f * kSampleRate + 1.0f);
}

// --- Long-run stability ---

TEST_CASE("CombFilter: long-run stability (10 seconds)", "[comb]")
{
    ideath::CombFilter comb;
    comb.prepare(kSampleRate, 0.05f);
    comb.setDelay(0.005f);
    comb.setFeedback(0.98f);
    comb.setDamp(0.3f);
    comb.setMix(1.0f);

    // 10 seconds: tests denormal protection in feedback loop (kAntiDenormal
    // in buffer write) and one-pole LP stability over 441k samples.
    comb.process(1.0f); // impulse
    constexpr int N = 441000;
    for (int i = 1; i < N; ++i)
    {
        float s = comb.process(0.0f);
        REQUIRE(std::isfinite(s));
    }
}

// --- Frequency selectivity ---

TEST_CASE("CombFilter: amplifies resonant frequency, attenuates off-resonance", "[comb]")
{
    // Comb filter resonates at f = n / delay_time. With delay = 1/440 Hz,
    // a 440 Hz sine lands on the first resonance peak; 330 Hz is off-peak.
    constexpr int N = 4096;
    float delayTime = 1.0f / 440.0f;

    auto measureRms = [&](float freqHz) {
        ideath::CombFilter comb;
        comb.prepare(kSampleRate, 0.01f);
        comb.setDelay(delayTime);
        comb.setFeedback(0.9f);
        comb.setDamp(0.0f);
        comb.setMix(1.0f);

        std::vector<float> buf(N);
        for (int i = 0; i < N; ++i)
        {
            float input = std::sin(2.0f * static_cast<float>(M_PI) * freqHz
                                   * static_cast<float>(i) / kSampleRate);
            buf[static_cast<size_t>(i)] = comb.process(input);
        }
        return rms(buf);
    };

    float rmsOnPeak  = measureRms(440.0f);  // on resonance
    float rmsOffPeak = measureRms(330.0f);  // off resonance (3/4 of 440)

    // On-resonance: constructive feedback builds up amplitude.
    // Off-resonance: destructive interference reduces amplitude.
    // Comb with fb=0.9: peak gain ≈ 1/(1−fb) = 10 (+20 dB) at resonance.
    // At anti-resonance (half-period offset): gain ≈ 1/(1+fb) = 0.526 (−5.6 dB).
    // 330 Hz is between resonance and anti-resonance, gain ≈ 1-2.
    // Ratio should be > 3× (≈10 dB).
    REQUIRE(rmsOnPeak > rmsOffPeak * 3.0f);
}

// --- Extreme parameter combinations ---

TEST_CASE("CombFilter: minimum delay + max feedback stays bounded", "[comb]")
{
    ideath::CombFilter comb;
    comb.prepare(kSampleRate, 0.01f);
    // Minimum delay = 1 sample (clamped). Max feedback = 0.999.
    comb.setDelay(0.0f);
    comb.setFeedback(1.0f); // clamped to 0.999
    comb.setDamp(0.0f);
    comb.setMix(1.0f);

    constexpr int N = 44100;
    for (int i = 0; i < N; ++i)
    {
        float input = (i == 0) ? 1.0f : 0.0f;
        float s = comb.process(input);
        REQUIRE(std::isfinite(s));
        // feedback=0.999 → geometric decay, never grows. Peak = 1.0.
        REQUIRE(std::fabs(s) <= 1.5f);
    }
}
