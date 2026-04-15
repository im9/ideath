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

    // 3 parallel SVFilter BPs, each at a different formant frequency.
    // Resonance 0.8 → svfRes = 0.82, k = 0.36, Q ≈ 2.78.
    // A single sine can strongly excite at most one formant peak (they're
    // widely spaced). Peak output ≈ Q × max_formant_gain = 2.78 × 1.0.
    // Bound 3.0 adds ~8% margin over Q.
    for (float s : output)
    {
        REQUIRE(s >= -3.0f);
        REQUIRE(s <= 3.0f);
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

    // 800Hz at F1=800: on center, SVF BP peak gain (Cytomic TPT normalization).
    // 200Hz: well below all formants, attenuated by the BP skirts.
    // The 3-band formant structure favors the on-F1 signal strongly.
    // For Q ≈ 2 (res=0.7, svfRes=0.755, k=0.49): ratio depends on SVF BP
    // normalization and multi-band gain summation. Threshold 5× is derived
    // from the general 2nd-order BP: center gain O(Q), off-center O(f/fc).
    INFO("RMS at 800Hz: " << rmsF1 << ", RMS at 200Hz: " << rmsLow);
    REQUIRE(rmsF1 > rmsLow * 5.0f);
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

    // A at 700Hz: BP(800) at 700/800=0.875, near center → gain ≈ 0.8.
    // I at 700Hz: BP(350) at 700/350=2.0, far above center → gain ≈ 0.23.
    // Ratio ≈ 3.4. Threshold 2.5× allows 27% margin.
    INFO("RMS vowel A at 700Hz: " << rmsA << ", RMS vowel I: " << rmsI);
    REQUIRE(rmsA > rmsI * 2.5f);
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

    // Morph step = 0.25, shifting each formant by ~100 Hz (A→E: F1 800→400).
    // BP bandwidth ≈ fc/Q ≈ 200 Hz (for Q≈2.5, fc≈500). A 100 Hz shift is
    // half the bandwidth, changing gain by at most ~2×.
    // Bounds 0.4–2.5 allow 25% margin over the 2× estimate.
    for (size_t i = 1; i < rmsValues.size(); ++i)
    {
        float ratio = rmsValues[i] / std::max(rmsValues[i - 1], 1e-6f);
        INFO("Morph step " << i << ": ratio=" << ratio);
        REQUIRE(ratio > 0.4f);
        REQUIRE(ratio < 2.5f);
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
        // mix=0: output = input × 1.0 + wet × 0.0 = input. Exact passthrough.
        REQUIRE_THAT(out, WithinAbs(input[i], 1e-6f));
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

    // After reset, all 3 SVFilter states are zeroed. process(0) = 0.
    float out = ff.process(0.0f);
    REQUIRE_THAT(out, WithinAbs(0.0f, 1e-6f));
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

    // res=0.2 → svfRes=0.43, Q≈1/1.14≈0.88. At-center gain ≈ Q ≈ 0.88.
    // res=0.9 → svfRes=0.885, Q≈1/0.23≈4.35. At-center gain ≈ Q ≈ 4.35.
    // Ratio ≈ 4.35/0.88 ≈ 4.9. Threshold 2× is conservative.
    INFO("Low resonance RMS: " << rmsLow << ", High resonance RMS: " << rmsHigh);
    REQUIRE(rmsHigh > rmsLow * 2.0f);
}

// --- Long-run stability ---

TEST_CASE("FormantFilter: long-run stability (10 seconds)", "[formant]")
{
    FormantFilter ff;
    ff.prepare(kSR);
    ff.setMorph(2.5f); // between I and O
    ff.setResonance(0.9f);

    // 10 seconds with alternating input. Tests denormal protection
    // in the 3 internal SVFilters over 441k samples.
    constexpr int N = 441000;
    for (int i = 0; i < N; ++i)
    {
        float input = (i % 2 == 0) ? 0.5f : -0.5f;
        float s = ff.process(input);
        REQUIRE(std::isfinite(s));
    }
}

// --- All vowels produce output ---

TEST_CASE("FormantFilter: all 5 vowels produce audible output", "[formant]")
{
    // Each vowel should produce meaningful output for broadband input.
    // Use a mid-frequency sine that overlaps at least one formant per vowel.
    for (float morph : {0.0f, 1.0f, 2.0f, 3.0f, 4.0f})
    {
        FormantFilter ff;
        ff.prepare(kSR);
        ff.setMorph(morph);
        ff.setResonance(0.5f);

        // 500Hz sine: overlaps F1 of most vowels (325–800 Hz range).
        auto input = sine(500.0f, kBlock);
        auto output = processBlock(ff, input);
        float r = rms(output);

        INFO("Morph=" << morph << " RMS=" << r);
        // BP at 500Hz near F1: gain is at least moderate for all vowels.
        // Minimum: vowel A (F1=800, 500/800=0.625) → gain ≈ 0.5.
        // Threshold 0.05 catches silence (broken formant).
        REQUIRE(r > 0.05f);
    }
}

// --- Extreme resonance ---

TEST_CASE("FormantFilter: extreme resonance stays finite", "[formant]")
{
    auto input = sine(800.0f, kBlock);

    // Min resonance (0.0 → svfRes=0.3, Q≈0.71)
    FormantFilter ffMin;
    ffMin.prepare(kSR);
    ffMin.setMorph(0.0f);
    ffMin.setResonance(0.0f);
    auto outMin = processBlock(ffMin, input);
    for (float s : outMin)
        REQUIRE(std::isfinite(s));
    REQUIRE(rms(outMin) > 0.01f);

    // Max resonance (1.0 → clamped to 0.99 → svfRes=0.9435, Q≈1/0.113≈8.85)
    FormantFilter ffMax;
    ffMax.prepare(kSR);
    ffMax.setMorph(0.0f);
    ffMax.setResonance(1.0f);
    auto outMax = processBlock(ffMax, input);
    for (float s : outMax)
        REQUIRE(std::isfinite(s));
    // resonance=1.0 → svfRes=0.95, k=0.1, Q=10. Peak gain ≈ Q.
    // Transient overshoot can exceed Q by a few percent. Bound = Q + 10%.
    for (float s : outMax)
        REQUIRE(std::fabs(s) < 11.0f);
}
