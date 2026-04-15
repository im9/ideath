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
    // 4-bit normalization: (val / 7.5) - 1.0 maps [0,15] → [-1, +1]
    float expected[] = {
        (0.0f / 7.5f) - 1.0f,   // -1.0
        (5.0f / 7.5f) - 1.0f,   // -0.333...
        (10.0f / 7.5f) - 1.0f,  //  0.333...
        (15.0f / 7.5f) - 1.0f   //  1.0
    };

    ideath::Wavetable wt;
    // Set sample rate so that freq produces exactly 4 samples per cycle
    // phaseInc = 440 / 1760 = 0.25 → phase hits {0.25, 0.5, 0.75, 0.0} exactly
    float sr = 4.0f * 440.0f; // 1760
    wt.prepare(sr);
    wt.setTable(data, 4);
    wt.setFrequency(440.0f);
    wt.setInterpolation(ideath::Wavetable::Interpolation::Nearest);

    // Phase advances before read, so first sample reads index 1, etc.
    // Exact integer phase positions → nearest lookup returns exact table values.
    // Tolerance: float32 arithmetic precision (~1e-7 for values near 1.0)
    for (int i = 0; i < 4; ++i)
    {
        float s = wt.process();
        int expectedIndex = (i + 1) % 4;
        REQUIRE_THAT(s, WithinAbs(expected[expectedIndex], 1e-6f));
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

    // Saw table: values monotonically increase from index 0 to 31, then wrap.
    // With sr=32, freq=1 Hz, phaseInc=1/32: phase hits each table index exactly.
    // Phase advances before read → indices visited: 1,2,...,31,0.
    // 31 transitions are rising (1→2,...,30→31) plus initial (prev=-2 → index 1).
    // Only the final transition (index 31→0) is falling. So 31 of 32 are rising.
    REQUIRE(rising >= 31);
}

TEST_CASE("Wavetable: sine factory DC offset near zero", "[wavetable]")
{
    auto wt = ideath::Wavetable::sineTable(64);
    wt.prepare(kSampleRate);
    wt.setFrequency(440.0f);

    // 44100 samples at 440 Hz ≈ 440.0 complete cycles (44100/440 = 100.227 samples/cycle).
    // Residual DC from the ~0.227 incomplete samples: peak contribution ≈ 1/44100 ≈ 2e-5.
    // 64-point symmetric sine table has zero inherent DC bias.
    // Tolerance 0.01 (−40 dB below unity) is a conservative "inaudible DC" bound.
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

    // Triangle table (size 64): exact +1.0 at index 32 (t=0.5, 3−4×0.5=1.0),
    // exact −1.0 at index 0 (t=0, 4×0−1=−1.0).
    // Nearest interpolation reads each index's bin when phase lands in
    // [i/64, (i+1)/64). Bin width = 1/64 ≈ 0.0156, phase step = 440/44100 ≈ 0.00998.
    // Since step < bin width, every bin is guaranteed to be visited — peak indices
    // 0 and 32 are hit, returning exact ±1.0. Tolerance is float precision only.
    REQUIRE_THAT(maxVal, WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(minVal, WithinAbs(-1.0f, 1e-6f));
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

TEST_CASE("Wavetable: reset preserves frequency", "[wavetable]")
{
    // reset() clears phase but must preserve phaseInc (frequency setting).
    // Matches Biquad/SVFilter convention: reset = zero state, not unconfigured.
    auto wt = ideath::Wavetable::sawTable();
    wt.prepare(kSampleRate);
    wt.setFrequency(440.0f);

    for (int i = 0; i < 500; ++i)
        wt.process();

    wt.reset();

    ideath::Wavetable fresh = ideath::Wavetable::sawTable();
    fresh.prepare(kSampleRate);
    fresh.setFrequency(440.0f);

    for (int i = 0; i < 16; ++i)
    {
        float a = wt.process();
        float b = fresh.process();
        REQUIRE_THAT(a, WithinAbs(b, 1e-6f));
    }
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

    // Square wave (nearest, 32-sample table): output is exactly +1 or −1.
    // 2 zero crossings per cycle × 440 Hz = 880 crossings in 1 second (44100 samples).
    // 44100/440 = 100.227 samples/cycle → ~440 complete cycles.
    // ±2 accounts for partial cycles at start/end of the window.
    REQUIRE(zeroCrossings >= 876);
    REQUIRE(zeroCrossings <= 884);
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

    // Saw wave RMS = 1/√3 ≈ 0.577 (continuous); 32-sample table approximation
    // is close. Threshold 0.4 is ~30% below theoretical — catches silence or
    // near-silence without over-fitting to the exact discrete value.
    float level = rms(buf.data(), N);
    REQUIRE(level > 0.4f);
}

// --- Long-run stability ---

TEST_CASE("Wavetable: long-run phase stability (10 seconds)", "[wavetable]")
{
    auto wt = ideath::Wavetable::sineTable(64);
    wt.prepare(kSampleRate);
    wt.setFrequency(440.0f);

    // 10 seconds of continuous processing — tests phase accumulator wrap
    // (phase -= floor(phase)) prevents float precision loss over ~4.41M samples.
    constexpr int N = 441000; // 10 seconds at 44100 Hz
    float maxVal = -2.0f;
    float minVal = 2.0f;
    for (int i = 0; i < N; ++i)
    {
        float s = wt.process();
        if (s > maxVal) maxVal = s;
        if (s < minVal) minVal = s;
        // Output must stay in [-1, 1] — any drift means broken phase wrap
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
    }
    // Phase should still be in [0, 1) after 10 seconds
    REQUIRE(wt.getPhase() >= 0.0f);
    REQUIRE(wt.getPhase() < 1.0f);
}

// --- Parameter boundary behavior ---

TEST_CASE("Wavetable: frequency 0 Hz produces constant output", "[wavetable]")
{
    auto wt = ideath::Wavetable::sawTable();
    wt.prepare(kSampleRate);
    wt.setFrequency(0.0f);

    // phaseInc = 0 → phase stuck at 0 after reset.
    // First process: phase += 0, phase = 0, reads index 0 every sample.
    float first = wt.process();
    for (int i = 1; i < 1000; ++i)
    {
        float s = wt.process();
        REQUIRE(s == first);
    }
}

TEST_CASE("Wavetable: frequency at Nyquist stays in range", "[wavetable]")
{
    auto wt = ideath::Wavetable::sineTable(64);
    wt.prepare(kSampleRate);
    // setFrequency clamps to sampleRate * 0.5 = 22050 Hz
    wt.setFrequency(22050.0f);

    constexpr int N = 44100;
    for (int i = 0; i < N; ++i)
    {
        float s = wt.process();
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
    }
}

TEST_CASE("Wavetable: frequency above Nyquist is clamped", "[wavetable]")
{
    // Two instances: one at Nyquist, one at 2× Nyquist — both should
    // produce identical output since setFrequency clamps to sr*0.5.
    auto wt1 = ideath::Wavetable::sineTable(64);
    auto wt2 = ideath::Wavetable::sineTable(64);
    wt1.prepare(kSampleRate);
    wt2.prepare(kSampleRate);
    wt1.setFrequency(22050.0f);
    wt2.setFrequency(99999.0f); // clamped to 22050

    for (int i = 0; i < 1000; ++i)
    {
        float s1 = wt1.process();
        float s2 = wt2.process();
        REQUIRE(s1 == s2);
    }
}

TEST_CASE("Wavetable: single-sample table produces constant", "[wavetable]")
{
    // Minimum table size: 1 sample. Regardless of frequency, output = that one value.
    float val = 7.5f; // 4-bit mid → (7.5/7.5) - 1.0 = 0.0
    ideath::Wavetable wt;
    wt.prepare(kSampleRate);
    wt.setTable(&val, 1);
    wt.setFrequency(440.0f);

    for (int i = 0; i < 1000; ++i)
    {
        float s = wt.process();
        // (7.5 / 7.5) - 1.0 = 0.0 exactly
        REQUIRE_THAT(s, WithinAbs(0.0f, 1e-6f));
    }
}

// --- Extreme parameter combinations ---

TEST_CASE("Wavetable: max table size + high frequency + linear interp", "[wavetable]")
{
    // 256-sample table at near-Nyquist: phase step ≈ 0.5 → each process()
    // skips ~128 table entries. Linear interpolation must not produce out-of-range.
    float data[256];
    for (int i = 0; i < 256; ++i)
        data[i] = static_cast<float>(i % 16); // 4-bit pattern repeated 16×
    ideath::Wavetable wt;
    wt.prepare(kSampleRate);
    wt.setTable(data, 256);
    wt.setFrequency(20000.0f);
    wt.setInterpolation(ideath::Wavetable::Interpolation::Linear);

    constexpr int N = 44100;
    for (int i = 0; i < N; ++i)
    {
        float s = wt.process();
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
    }
}

TEST_CASE("Wavetable: tiny table + low frequency + linear interp", "[wavetable]")
{
    // 2-sample table at 1 Hz: phaseInc ≈ 2.27e-5. Many samples per table step.
    // Linear interp between 2 values should produce smooth ramp.
    float data[] = { 0.0f, 15.0f }; // 4-bit: normalized to {-1.0, +1.0}
    ideath::Wavetable wt;
    wt.prepare(kSampleRate);
    wt.setTable(data, 2);
    wt.setFrequency(1.0f);
    wt.setInterpolation(ideath::Wavetable::Interpolation::Linear);

    constexpr int N = 44100; // 1 second = 1 cycle
    for (int i = 0; i < N; ++i)
    {
        float s = wt.process();
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
    }
}

// --- 4-bit normalization correctness ---

TEST_CASE("Wavetable: 4-bit normalization maps [0,15] to [-1,+1]", "[wavetable]")
{
    // setTable divides by 7.5 and subtracts 1.0:
    //   0 → (0/7.5) − 1 = −1.0
    //   7.5 → (7.5/7.5) − 1 = 0.0  (midpoint)
    //  15 → (15/7.5) − 1 = +1.0
    // This maps the Game Boy 4-bit range [0,15] linearly to [-1,+1].
    float data[] = { 0.0f, 7.5f, 15.0f };
    ideath::Wavetable wt;
    float sr = 3.0f * 100.0f; // phaseInc = 100/300 = 1/3 → hits each index
    wt.prepare(sr);
    wt.setTable(data, 3);
    wt.setFrequency(100.0f);
    wt.setInterpolation(ideath::Wavetable::Interpolation::Nearest);

    // Phase advances before read: phase = {1/3, 2/3, 0} → indices {1, 2, 0}
    float s0 = wt.process(); // index 1 → 7.5/7.5 − 1 = 0.0
    float s1 = wt.process(); // index 2 → 15/7.5 − 1 = +1.0
    float s2 = wt.process(); // index 0 → 0/7.5 − 1 = −1.0

    REQUIRE_THAT(s0, WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(s1, WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(s2, WithinAbs(-1.0f, 1e-6f));
}
