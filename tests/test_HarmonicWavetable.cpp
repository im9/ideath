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

    // Both readTable (within-table linear interp) and morph crossfade are
    // convex combinations of values in [-1, 1] (peak-normalized tables).
    // Convex combination: result ∈ [min(a,b), max(a,b)] ⊂ [-1, 1].
    // Float rounding: at most a few ULPs, covered by 1e-6 margin.
    for (float morph = 0.0f; morph <= 1.0f; morph += 0.25f)
    {
        hw.setMorphPosition(morph);
        hw.reset();
        for (int i = 0; i < 44100; ++i)
        {
            float s = hw.process();
            REQUIRE(s >= -1.0f - 1e-6f);
            REQUIRE(s <= 1.0f + 1e-6f);
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

    // Pure sine: DC ≈ 0. 44100 samples at 440 Hz ≈ 440 complete cycles.
    // Residual DC from ~0.227 incomplete samples: ≈ 1/44100 ≈ 2e-5.
    // Tolerance 0.01 (−40 dB) is a conservative "inaudible DC" bound.
    REQUIRE_THAT(dc, WithinAbs(0.0f, 0.01f));
    // Sine RMS = 1/√2 ≈ 0.70711. 256-point table with linear interpolation:
    // max per-sample error ≈ (2π/256)² / 8 ≈ 7.5e-5 (Taylor remainder of sin).
    // Over ~440 complete cycles the RMS error is negligible. Tolerance 0.01
    // (1.4% relative) is well above the interpolation error.
    REQUIRE_THAT(rmsVal, WithinAbs(0.707f, 0.01f));
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

    // Morph 0.499 vs 0.501: Δmorph = 0.002 maps to Δtable = 0.002 × 127 = 0.254
    // in crossfade fraction between tables 63 and 64.
    // Adjacent tables differ by one harmonic: the 65th at amplitude 1/65 ≈ 0.0154
    // (pre-normalization). After peak normalization the difference at any given
    // phase is at most ~0.02. Output delta = 0.254 × table_delta ≈ 0.005.
    // Tolerance 0.02 is ~4× the expected difference — guards continuity without
    // over-fitting.
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

    REQUIRE_THAT(sA, WithinAbs(sB, 0.02f));
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

    // Pure sine: 2 zero crossings per cycle × 440 Hz = 880 in 1 second.
    // 44100/440 = 100.227 samples/cycle → ~440 complete cycles.
    // ±4 accounts for partial cycles at window boundaries.
    REQUIRE(zeroCrossings >= 876);
    REQUIRE(zeroCrossings <= 884);
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
        // All tables are peak-normalized to [-1, 1]. For a sine (morph=0),
        // RMS = 1/√2 ≈ 0.707. For harmonically rich tables (higher morph),
        // crest factor increases but RMS stays above ~0.5. Threshold 0.4
        // is ~30% below the minimum expected RMS.
        float rmsVal = static_cast<float>(std::sqrt(sumSq / N));
        REQUIRE(rmsVal > 0.4f);
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

    // At 10kHz, harmonics 3+ exceed Nyquist (22050 Hz) and are band-limited out.
    // Tables are peak-normalized; linear interp is convex — output stays in [-1, 1].
    // Float rounding margin: 1e-6.
    for (int i = 0; i < 44100; ++i)
    {
        float s = hw.process();
        REQUIRE(s >= -1.0f - 1e-6f);
        REQUIRE(s <= 1.0f + 1e-6f);
    }
}

// --- Long-run stability ---

TEST_CASE("HarmonicWavetable: long-run phase stability (10 seconds)", "[harmonic_wavetable]")
{
    ideath::HarmonicWavetable hw;
    hw.prepare(kSampleRate);
    hw.setFrequency(440.0f);
    hw.setMorphPosition(0.75f);

    // 10 seconds at 44100 Hz = 441000 samples.
    // Phase accumulator uses phase -= floor(phase) every sample.
    // Verifies no precision drift or denormal accumulation over long runs.
    constexpr int N = 441000;
    for (int i = 0; i < N; ++i)
    {
        float s = hw.process();
        REQUIRE(s >= -1.0f - 1e-6f);
        REQUIRE(s <= 1.0f + 1e-6f);
    }
    // Phase must remain in [0, 1)
    REQUIRE(hw.getPhase() >= 0.0f);
    REQUIRE(hw.getPhase() < 1.0f);
}

// --- Parameter boundary behavior ---

TEST_CASE("HarmonicWavetable: frequency 0 Hz produces constant output", "[harmonic_wavetable]")
{
    ideath::HarmonicWavetable hw;
    hw.prepare(kSampleRate);
    hw.setFrequency(0.0f);
    hw.setMorphPosition(0.5f);

    // phaseInc = 0 → phase stays at 0 → same table position every sample.
    float first = hw.process();
    for (int i = 1; i < 1000; ++i)
    {
        float s = hw.process();
        REQUIRE(s == first);
    }
}

TEST_CASE("HarmonicWavetable: frequency at Nyquist limit stays in range", "[harmonic_wavetable]")
{
    ideath::HarmonicWavetable hw;
    hw.prepare(kSampleRate);
    // setFrequency clamps to sampleRate * 0.45 = 19845 Hz
    hw.setFrequency(22050.0f);
    hw.setMorphPosition(1.0f);

    constexpr int N = 44100;
    for (int i = 0; i < N; ++i)
    {
        float s = hw.process();
        REQUIRE(s >= -1.0f - 1e-6f);
        REQUIRE(s <= 1.0f + 1e-6f);
    }
}

TEST_CASE("HarmonicWavetable: morph=0 and morph=1 boundary tables", "[harmonic_wavetable]")
{
    // morph=0: table 0 (fundamental only, sine-like). morph=1: table 127 (128 harmonics).
    // Both should produce valid output with distinct timbres.
    constexpr int N = 4096;

    auto measureRmsDiff = [&](float morph) {
        ideath::HarmonicWavetable hw;
        hw.prepare(kSampleRate);
        hw.setFrequency(200.0f);
        hw.setMorphPosition(morph);
        double sumSq = 0.0;
        float prev = hw.process();
        for (int i = 1; i < N; ++i)
        {
            float s = hw.process();
            double d = static_cast<double>(s - prev);
            sumSq += d * d;
            prev = s;
        }
        return static_cast<float>(std::sqrt(sumSq / (N - 1)));
    };

    float bright0 = measureRmsDiff(0.0f);
    float bright1 = measureRmsDiff(1.0f);
    // Table 127 must be distinctly brighter than table 0 (more harmonics).
    // Ratio > 2× is expected: sine first-diff RMS is modest, while a
    // 128-harmonic waveform has much steeper slopes.
    REQUIRE(bright1 > bright0 * 2.0f);
}

// --- Extreme parameter combinations ---

TEST_CASE("HarmonicWavetable: high freq + high morph (aliasing stress)", "[harmonic_wavetable]")
{
    // Near-Nyquist frequency with maximum harmonic content.
    // Band-limiting should suppress all harmonics above Nyquist.
    ideath::HarmonicWavetable hw;
    hw.prepare(kSampleRate);
    hw.setFrequency(15000.0f);
    hw.setMorphPosition(1.0f);

    constexpr int N = 44100;
    for (int i = 0; i < N; ++i)
    {
        float s = hw.process();
        REQUIRE(s >= -1.0f - 1e-6f);
        REQUIRE(s <= 1.0f + 1e-6f);
    }
}

TEST_CASE("HarmonicWavetable: low freq + full morph sweep", "[harmonic_wavetable]")
{
    // 20 Hz with morph sweeping 0→1 over 2 seconds.
    // Low frequency means many harmonics fit below Nyquist — complex waveform.
    ideath::HarmonicWavetable hw;
    hw.prepare(kSampleRate);
    hw.setFrequency(20.0f);

    constexpr int N = 88200; // 2 seconds
    for (int i = 0; i < N; ++i)
    {
        float morph = static_cast<float>(i) / static_cast<float>(N);
        hw.setMorphPosition(morph);
        float s = hw.process();
        REQUIRE(s >= -1.0f - 1e-6f);
        REQUIRE(s <= 1.0f + 1e-6f);
    }
}
