#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/HarmonicWavetable.h>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

// --- Output range ---

TEST_CASE("HarmonicWavetable: output in [-1, 1]", "[harmonic_wavetable]")
{
    ideath::HarmonicWavetable hw;
    hw.prepare(kSampleRate);
    hw.setFrequency(440.0f);

    for (float morph = 0.0f; morph <= 1.0f; morph += 0.25f)
    {
        hw.setMorphPosition(morph);
        hw.reset();
        for (int i = 0; i < 44100; ++i)
        {
            float s = hw.process();
            REQUIRE(s >= -1.01f); // tiny tolerance for interpolation
            REQUIRE(s <= 1.01f);
        }
    }
}

// --- Table 0 is a pure sine ---

TEST_CASE("HarmonicWavetable: table 0 is pure sine", "[harmonic_wavetable]")
{
    ideath::HarmonicWavetable hw;
    hw.prepare(kSampleRate);
    hw.setFrequency(440.0f);
    hw.setMorphPosition(0.0f);

    // Collect one second of output and measure DC offset and RMS
    constexpr int N = 44100;
    double sum = 0.0;
    double sumSq = 0.0;
    for (int i = 0; i < N; ++i)
    {
        double s = static_cast<double>(hw.process());
        sum += s;
        sumSq += s * s;
    }

    float dc = static_cast<float>(sum / N);
    float rmsVal = static_cast<float>(std::sqrt(sumSq / N));

    // Pure sine: DC ≈ 0, RMS ≈ 1/√2 ≈ 0.707
    REQUIRE_THAT(dc, WithinAbs(0.0f, 0.01f));
    REQUIRE_THAT(rmsVal, WithinAbs(0.707f, 0.05f));
}

// --- Morph increases harmonic content (higher RMS of derivative = brighter) ---

TEST_CASE("HarmonicWavetable: higher morph = brighter spectrum", "[harmonic_wavetable]")
{
    constexpr int N = 44100;

    auto measureBrightness = [&](float morph) -> float {
        ideath::HarmonicWavetable hw;
        hw.prepare(kSampleRate);
        hw.setFrequency(200.0f);
        hw.setMorphPosition(morph);

        // Brightness proxy: RMS of first difference (higher = more HF content)
        float prev = hw.process();
        double diffSumSq = 0.0;
        for (int i = 1; i < N; ++i)
        {
            float s = hw.process();
            double d = static_cast<double>(s - prev);
            diffSumSq += d * d;
            prev = s;
        }
        return static_cast<float>(std::sqrt(diffSumSq / (N - 1)));
    };

    float bright0 = measureBrightness(0.0f);
    float bright50 = measureBrightness(0.5f);
    float bright100 = measureBrightness(1.0f);

    REQUIRE(bright50 > bright0);
    REQUIRE(bright100 > bright50);
}

// --- Morph interpolation is continuous ---

TEST_CASE("HarmonicWavetable: morph interpolation is continuous", "[harmonic_wavetable]")
{
    ideath::HarmonicWavetable hw;
    hw.prepare(kSampleRate);
    hw.setFrequency(440.0f);

    // Sample at morph=0.501 should be close to morph=0.499
    hw.setMorphPosition(0.499f);
    hw.reset();
    // Advance some samples to get away from phase=0
    for (int i = 0; i < 100; ++i)
        hw.process();
    float sA = hw.process();

    hw.setMorphPosition(0.501f);
    hw.reset();
    for (int i = 0; i < 100; ++i)
        hw.process();
    float sB = hw.process();

    REQUIRE_THAT(sA, WithinAbs(sB, 0.05f));
}

// --- Frequency correctness ---

TEST_CASE("HarmonicWavetable: correct frequency via zero crossings", "[harmonic_wavetable]")
{
    ideath::HarmonicWavetable hw;
    hw.prepare(kSampleRate);
    hw.setFrequency(440.0f);
    hw.setMorphPosition(0.0f); // pure sine for clean zero crossings

    constexpr int N = 44100;
    int zeroCrossings = 0;
    float prev = hw.process();
    for (int i = 1; i < N; ++i)
    {
        float s = hw.process();
        if ((prev >= 0.0f && s < 0.0f) || (prev < 0.0f && s >= 0.0f))
            ++zeroCrossings;
        prev = s;
    }

    // Sine: 2 zero crossings per cycle → ~880
    REQUIRE(zeroCrossings >= 850);
    REQUIRE(zeroCrossings <= 910);
}

// --- Reset ---

TEST_CASE("HarmonicWavetable: reset returns phase to zero", "[harmonic_wavetable]")
{
    ideath::HarmonicWavetable hw;
    hw.prepare(kSampleRate);
    hw.setFrequency(440.0f);
    hw.setMorphPosition(0.5f);

    for (int i = 0; i < 500; ++i)
        hw.process();

    REQUIRE(hw.getPhase() > 0.0f);
    hw.reset();
    REQUIRE_THAT(hw.getPhase(), WithinAbs(0.0f, 1e-6f));
}

// --- Parameter clamping ---

TEST_CASE("HarmonicWavetable: morph position clamped to [0, 1]", "[harmonic_wavetable]")
{
    ideath::HarmonicWavetable hw;
    hw.prepare(kSampleRate);

    hw.setMorphPosition(-0.5f);
    REQUIRE_THAT(hw.getMorphPosition(), WithinAbs(0.0f, 1e-6f));

    hw.setMorphPosition(1.5f);
    REQUIRE_THAT(hw.getMorphPosition(), WithinAbs(1.0f, 1e-6f));
}

// --- Produces nonzero output ---

TEST_CASE("HarmonicWavetable: produces nonzero output at all morph positions", "[harmonic_wavetable]")
{
    for (float morph : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
    {
        ideath::HarmonicWavetable hw;
        hw.prepare(kSampleRate);
        hw.setFrequency(440.0f);
        hw.setMorphPosition(morph);

        double sumSq = 0.0;
        constexpr int N = 4096;
        for (int i = 0; i < N; ++i)
        {
            double s = static_cast<double>(hw.process());
            sumSq += s * s;
        }
        float rmsVal = static_cast<float>(std::sqrt(sumSq / N));
        REQUIRE(rmsVal > 0.1f);
    }
}

// --- Band-limiting: high morph at high freq doesn't alias ---

TEST_CASE("HarmonicWavetable: band-limited at high frequency", "[harmonic_wavetable]")
{
    // At 10kHz, harmonics above ~22kHz should be suppressed
    ideath::HarmonicWavetable hw;
    hw.prepare(kSampleRate);
    hw.setFrequency(10000.0f);
    hw.setMorphPosition(1.0f);

    // Output should still be in range (no aliasing blow-up)
    for (int i = 0; i < 44100; ++i)
    {
        float s = hw.process();
        REQUIRE(s >= -1.01f);
        REQUIRE(s <= 1.01f);
    }
}
