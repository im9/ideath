#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Wavetable.h>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

static float rms(const float* buf, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += static_cast<double>(buf[i]) * static_cast<double>(buf[i]);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(n)));
}

TEST_CASE("Wavetable: output range [-1, 1] nearest", "[wavetable]")
{
    // Load all 16 possible 4-bit values
    float data[16];
    for (int i = 0; i < 16; ++i)
        data[i] = static_cast<float>(i);

    ideath::Wavetable wt;
    wt.prepare(kSampleRate);
    wt.setTable(data, 16);
    wt.setFrequency(440.0f);
    wt.setInterpolation(ideath::Wavetable::Interpolation::Nearest);

    constexpr int N = 44100;
    for (int i = 0; i < N; ++i)
    {
        float s = wt.process();
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
    }
}

TEST_CASE("Wavetable: output range [-1, 1] linear", "[wavetable]")
{
    float data[16];
    for (int i = 0; i < 16; ++i)
        data[i] = static_cast<float>(i);

    ideath::Wavetable wt;
    wt.prepare(kSampleRate);
    wt.setTable(data, 16);
    wt.setFrequency(440.0f);
    wt.setInterpolation(ideath::Wavetable::Interpolation::Linear);

    constexpr int N = 44100;
    for (int i = 0; i < N; ++i)
    {
        float s = wt.process();
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
    }
}

TEST_CASE("Wavetable: nearest reads exact table values", "[wavetable]")
{
    // 4-sample table: 0, 5, 10, 15
    float data[] = { 0.0f, 5.0f, 10.0f, 15.0f };
    float expected[] = {
        (0.0f / 7.5f) - 1.0f,   // -1.0
        (5.0f / 7.5f) - 1.0f,   // -0.333...
        (10.0f / 7.5f) - 1.0f,  //  0.333...
        (15.0f / 7.5f) - 1.0f   //  1.0
    };

    ideath::Wavetable wt;
    // Set sample rate so that freq produces exactly 4 samples per cycle
    float sr = 4.0f * 440.0f; // 1760
    wt.prepare(sr);
    wt.setTable(data, 4);
    wt.setFrequency(440.0f);
    wt.setInterpolation(ideath::Wavetable::Interpolation::Nearest);

    // Phase advances before read (same as Oscillator), so first sample
    // reads index 1, second reads index 2, etc.
    for (int i = 0; i < 4; ++i)
    {
        float s = wt.process();
        int expectedIndex = (i + 1) % 4;
        REQUIRE_THAT(s, WithinAbs(expected[expectedIndex], 0.01f));
    }
}

TEST_CASE("Wavetable: linear produces intermediate values", "[wavetable]")
{
    // 2-sample table: 0 and 15 → -1.0 and +1.0
    float data[] = { 0.0f, 15.0f };

    ideath::Wavetable wt;
    wt.prepare(kSampleRate);
    wt.setTable(data, 2);
    wt.setFrequency(100.0f);
    wt.setInterpolation(ideath::Wavetable::Interpolation::Linear);

    // Collect samples for one cycle
    int samplesPerCycle = static_cast<int>(kSampleRate / 100.0f);
    bool foundIntermediate = false;
    for (int i = 0; i < samplesPerCycle; ++i)
    {
        float s = wt.process();
        if (s > -0.99f && s < 0.99f)
            foundIntermediate = true;
    }
    REQUIRE(foundIntermediate);
}

TEST_CASE("Wavetable: square factory waveform", "[wavetable]")
{
    auto wt = ideath::Wavetable::squareTable();
    wt.prepare(kSampleRate);
    wt.setFrequency(440.0f);
    wt.setInterpolation(ideath::Wavetable::Interpolation::Nearest);

    // Collect one cycle worth of samples
    int samplesPerCycle = static_cast<int>(kSampleRate / 440.0f);

    int positiveCount = 0;
    int negativeCount = 0;
    for (int i = 0; i < samplesPerCycle; ++i)
    {
        float s = wt.process();
        if (s > 0.5f) ++positiveCount;
        else if (s < -0.5f) ++negativeCount;
    }

    // Square wave: roughly half positive, half negative
    REQUIRE(positiveCount > 0);
    REQUIRE(negativeCount > 0);
}

TEST_CASE("Wavetable: saw factory ramp", "[wavetable]")
{
    auto wt = ideath::Wavetable::sawTable(32);
    wt.prepare(32.0f * 1.0f); // 1 Hz at sr=32 → 1 sample per table entry
    wt.setFrequency(1.0f);
    wt.setInterpolation(ideath::Wavetable::Interpolation::Nearest);

    float prev = -2.0f;
    int rising = 0;
    for (int i = 0; i < 32; ++i)
    {
        float s = wt.process();
        if (s > prev) ++rising;
        prev = s;
    }

    // Most samples should be rising (saw ramp)
    REQUIRE(rising >= 28);
}

TEST_CASE("Wavetable: sine factory DC offset near zero", "[wavetable]")
{
    auto wt = ideath::Wavetable::sineTable(64);
    wt.prepare(kSampleRate);
    wt.setFrequency(440.0f);

    constexpr int N = 44100;
    double sum = 0.0;
    for (int i = 0; i < N; ++i)
        sum += static_cast<double>(wt.process());

    float dc = static_cast<float>(sum / N);
    REQUIRE_THAT(dc, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("Wavetable: triangle factory symmetry", "[wavetable]")
{
    auto wt = ideath::Wavetable::triangleTable(64);
    wt.prepare(kSampleRate);
    wt.setFrequency(440.0f);
    wt.setInterpolation(ideath::Wavetable::Interpolation::Nearest);

    constexpr int N = 44100;
    float maxVal = -2.0f;
    float minVal = 2.0f;
    for (int i = 0; i < N; ++i)
    {
        float s = wt.process();
        if (s > maxVal) maxVal = s;
        if (s < minVal) minVal = s;
    }

    REQUIRE_THAT(maxVal, WithinAbs(1.0f, 0.05f));
    REQUIRE_THAT(minVal, WithinAbs(-1.0f, 0.05f));
}

TEST_CASE("Wavetable: reset returns phase to zero", "[wavetable]")
{
    auto wt = ideath::Wavetable::squareTable();
    wt.prepare(kSampleRate);
    wt.setFrequency(1000.0f);

    for (int i = 0; i < 500; ++i)
        wt.process();

    REQUIRE(wt.getPhase() > 0.0f);
    wt.reset();
    REQUIRE_THAT(wt.getPhase(), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("Wavetable: setTable clamps size", "[wavetable]")
{
    ideath::Wavetable wt;

    // Over max
    std::vector<float> big(512, 7.5f);
    wt.setTable(big.data(), 512);
    REQUIRE(wt.getTableSize() == ideath::Wavetable::kMaxTableSize);

    // Zero or negative → clamp to 1
    float val = 7.5f;
    wt.setTable(&val, 0);
    REQUIRE(wt.getTableSize() == 1);

    wt.setTable(&val, -5);
    REQUIRE(wt.getTableSize() == 1);
}

TEST_CASE("Wavetable: frequency correctness via zero crossings", "[wavetable]")
{
    auto wt = ideath::Wavetable::squareTable();
    wt.prepare(kSampleRate);
    wt.setFrequency(440.0f);
    wt.setInterpolation(ideath::Wavetable::Interpolation::Nearest);

    constexpr int N = 44100;
    int zeroCrossings = 0;
    float prev = wt.process();
    for (int i = 1; i < N; ++i)
    {
        float s = wt.process();
        if ((prev >= 0.0f && s < 0.0f) || (prev < 0.0f && s >= 0.0f))
            ++zeroCrossings;
        prev = s;
    }

    // Square wave: 2 zero crossings per cycle → ~880
    REQUIRE(zeroCrossings >= 800);
    REQUIRE(zeroCrossings <= 960);
}

TEST_CASE("Wavetable: produces nonzero output", "[wavetable]")
{
    auto wt = ideath::Wavetable::sawTable();
    wt.prepare(kSampleRate);
    wt.setFrequency(440.0f);

    constexpr int N = 4096;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i)
        buf[static_cast<size_t>(i)] = wt.process();

    float level = rms(buf.data(), N);
    REQUIRE(level > 0.1f);
}
