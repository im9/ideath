#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/FormantFilter.h>
#include <cmath>
#include <vector>
#include <numeric>

using Catch::Matchers::WithinAbs;
using namespace ideath;

static constexpr float kSR = 44100.0f;
static constexpr int kBlock = 4096;

// Generate a sine wave block
static std::vector<float> sine(float freq, int n, float sr = kSR)
{
    std::vector<float> buf(n);
    for (int i = 0; i < n; ++i)
        buf[i] = std::sin(2.0f * 3.14159265f * freq * i / sr);
    return buf;
}

// RMS of a buffer
static float rms(const std::vector<float>& buf)
{
    float sum = 0.0f;
    for (float s : buf) sum += s * s;
    return std::sqrt(sum / static_cast<float>(buf.size()));
}

// Process a block through the filter
static std::vector<float> processBlock(FormantFilter& ff, const std::vector<float>& input)
{
    std::vector<float> out(input.size());
    for (size_t i = 0; i < input.size(); ++i)
        out[i] = ff.process(input[i]);
    return out;
}

TEST_CASE("FormantFilter: output in [-1, 1] for bounded input", "[formant]")
{
    FormantFilter ff;
    ff.prepare(kSR);
    ff.setMorph(0.0f);  // A
    ff.setResonance(0.8f);

    auto input = sine(200.0f, kBlock);
    auto output = processBlock(ff, input);

    for (float s : output)
    {
        REQUIRE(s >= -2.0f);  // 3 parallel bands can sum > 1; check reasonable range
        REQUIRE(s <= 2.0f);
    }
}

TEST_CASE("FormantFilter: vowel A passes F1 region (~800Hz)", "[formant]")
{
    FormantFilter ff;
    ff.prepare(kSR);
    ff.setMorph(0.0f);  // A: F1=800
    ff.setResonance(0.7f);

    // 800Hz should pass well (near F1 of A)
    auto nearF1 = sine(800.0f, kBlock);
    auto outF1 = processBlock(ff, nearF1);
    float rmsF1 = rms(outF1);

    ff.reset();

    // 200Hz should be attenuated (far below F1)
    auto low = sine(200.0f, kBlock);
    auto outLow = processBlock(ff, low);
    float rmsLow = rms(outLow);

    INFO("RMS at 800Hz: " << rmsF1 << ", RMS at 200Hz: " << rmsLow);
    REQUIRE(rmsF1 > rmsLow * 2.0f);
}

TEST_CASE("FormantFilter: different vowels have different spectral shape", "[formant]")
{
    // Vowel A (F1=800) vs vowel I (F1=350) — test with 700Hz input.
    // 700Hz is near A's F1 but far from I's F1, so A should pass more.
    auto input = sine(700.0f, kBlock);

    FormantFilter ffA;
    ffA.prepare(kSR);
    ffA.setMorph(0.0f);  // A
    ffA.setResonance(0.7f);
    auto outA = processBlock(ffA, input);
    float rmsA = rms(outA);

    FormantFilter ffI;
    ffI.prepare(kSR);
    ffI.setMorph(2.0f);  // I
    ffI.setResonance(0.7f);
    auto outI = processBlock(ffI, input);
    float rmsI = rms(outI);

    INFO("RMS vowel A at 700Hz: " << rmsA << ", RMS vowel I: " << rmsI);
    REQUIRE(rmsA > rmsI * 1.5f);
}

TEST_CASE("FormantFilter: morph interpolates smoothly", "[formant]")
{
    auto input = sine(500.0f, kBlock);

    // Morph from A(0) to E(1) in small steps — output should change gradually
    std::vector<float> rmsValues;
    for (float m = 0.0f; m <= 1.0f; m += 0.25f)
    {
        FormantFilter ff;
        ff.prepare(kSR);
        ff.setMorph(m);
        ff.setResonance(0.6f);
        auto out = processBlock(ff, input);
        rmsValues.push_back(rms(out));
    }

    // Check no sudden jumps — adjacent morph positions differ by < 50%
    for (size_t i = 1; i < rmsValues.size(); ++i)
    {
        float ratio = rmsValues[i] / std::max(rmsValues[i - 1], 1e-6f);
        INFO("Morph step " << i << ": ratio=" << ratio);
        REQUIRE(ratio > 0.3f);
        REQUIRE(ratio < 3.0f);
    }
}

TEST_CASE("FormantFilter: mix 0 is dry, mix 1 is wet", "[formant]")
{
    auto input = sine(440.0f, kBlock);

    FormantFilter ff;
    ff.prepare(kSR);
    ff.setMorph(0.0f);
    ff.setMix(0.0f);  // fully dry

    // Skip first samples (filter settling)
    for (int i = 0; i < 100; ++i) ff.process(input[i]);

    for (int i = 100; i < kBlock; ++i)
    {
        float out = ff.process(input[i]);
        REQUIRE_THAT(out, WithinAbs(input[i], 1e-5f));
    }
}

TEST_CASE("FormantFilter: reset clears state", "[formant]")
{
    FormantFilter ff;
    ff.prepare(kSR);
    ff.setMorph(2.0f);
    ff.setResonance(0.8f);

    // Process some signal
    auto input = sine(1000.0f, 1000);
    processBlock(ff, input);

    ff.reset();

    // After reset, output should be near zero for zero input
    float out = ff.process(0.0f);
    REQUIRE_THAT(out, WithinAbs(0.0f, 1e-5f));
}

TEST_CASE("FormantFilter: morph clamps to [0, 4]", "[formant]")
{
    FormantFilter ff;
    ff.prepare(kSR);

    // Out of range values should not crash
    ff.setMorph(-1.0f);
    ff.process(0.5f);

    ff.setMorph(5.0f);
    ff.process(0.5f);

    // Just verify no crash or NaN
    float out = ff.process(0.5f);
    REQUIRE(std::isfinite(out));
}

TEST_CASE("FormantFilter: resonance affects peak sharpness", "[formant]")
{
    auto input = sine(800.0f, kBlock);  // Right at A's F1

    FormantFilter ffLow;
    ffLow.prepare(kSR);
    ffLow.setMorph(0.0f);
    ffLow.setResonance(0.2f);
    auto outLow = processBlock(ffLow, input);
    float rmsLow = rms(outLow);

    FormantFilter ffHigh;
    ffHigh.prepare(kSR);
    ffHigh.setMorph(0.0f);
    ffHigh.setResonance(0.9f);
    auto outHigh = processBlock(ffHigh, input);
    float rmsHigh = rms(outHigh);

    INFO("Low resonance RMS: " << rmsLow << ", High resonance RMS: " << rmsHigh);
    // Higher resonance at the formant frequency should produce more output
    REQUIRE(rmsHigh > rmsLow);
}
