#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Biquad.h>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

// Helper: generate a sine wave buffer.
static std::vector<float> makeSine(float freqHz, float sampleRate, int numSamples, float amplitude = 1.0f)
{
    std::vector<float> buf(static_cast<size_t>(numSamples));
    const float inc = freqHz / sampleRate;
    float phase = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        buf[static_cast<size_t>(i)] = amplitude * std::sin(2.0f * static_cast<float>(M_PI) * phase);
        phase += inc;
        phase -= std::floor(phase);
    }
    return buf;
}

// Helper: RMS level of a buffer.
static float rms(const std::vector<float>& buf)
{
    double sum = 0.0;
    for (auto s : buf)
        sum += static_cast<double>(s) * static_cast<double>(s);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(buf.size())));
}

// Helper: process a buffer through a Biquad and return output.
static std::vector<float> processBuffer(ideath::Biquad& bq, const std::vector<float>& input)
{
    std::vector<float> output(input.size());
    for (size_t i = 0; i < input.size(); ++i)
        output[i] = bq.process(input[i]);
    return output;
}

TEST_CASE("Biquad: default state is passthrough", "[biquad]")
{
    ideath::Biquad bq;
    // Default coefficients: b0=1, rest=0 → output == input.
    REQUIRE_THAT(bq.process(0.5f), WithinAbs(0.5f, 1e-6f));
    REQUIRE_THAT(bq.process(-0.3f), WithinAbs(-0.3f, 1e-6f));
    REQUIRE_THAT(bq.process(0.0f), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("Biquad: reset clears state", "[biquad]")
{
    ideath::Biquad bq;
    bq.setLowpass(1000.0f, 0.707f, kSampleRate);
    // Feed some signal.
    for (int i = 0; i < 100; ++i)
        bq.process(1.0f);
    bq.reset();
    // After reset, first sample should not carry history.
    // Process a 0 — should output 0 (no ringing from state).
    REQUIRE_THAT(bq.process(0.0f), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("Biquad lowpass: attenuates high frequencies", "[biquad]")
{
    ideath::Biquad bq;
    bq.setLowpass(500.0f, 0.707f, kSampleRate);

    constexpr int N = 4096;

    // 100 Hz sine — should pass through mostly intact.
    auto low = makeSine(100.0f, kSampleRate, N);
    auto lowOut = processBuffer(bq, low);
    float lowRms = rms(lowOut);

    bq.reset();

    // 5000 Hz sine — should be heavily attenuated.
    auto high = makeSine(5000.0f, kSampleRate, N);
    auto highOut = processBuffer(bq, high);
    float highRms = rms(highOut);

    // 2nd-order Butterworth LP (Q=0.707) at fc=500Hz:
    // |H(f)|² = 1 / ((1−(f/fc)²)² + (f/(fc·Q))²)
    // At 100Hz (f/fc=0.2): |H| ≈ 0.999 ≈ −0.009 dB (deep passband).
    // At 5000Hz (f/fc=10): |H| ≈ 0.01 ≈ −40 dB (12 dB/oct × 3.32 octaves).
    // Expected ratio ≈ 40 dB. Threshold 35 dB allows 5 dB margin for the
    // transient in the 4096-sample window (filter settles in ~20 samples).
    float ratioDb = 20.0f * std::log10(lowRms / std::max(highRms, 1e-10f));
    REQUIRE(ratioDb > 35.0f);
}

TEST_CASE("Biquad highpass: attenuates low frequencies", "[biquad]")
{
    ideath::Biquad bq;
    bq.setHighpass(2000.0f, 0.707f, kSampleRate);

    constexpr int N = 4096;

    // 5000 Hz — should pass.
    auto high = makeSine(5000.0f, kSampleRate, N);
    auto highOut = processBuffer(bq, high);
    float highRms = rms(highOut);

    bq.reset();

    // 100 Hz — should be attenuated.
    auto low = makeSine(100.0f, kSampleRate, N);
    auto lowOut = processBuffer(bq, low);
    float lowRms = rms(lowOut);

    // 2nd-order Butterworth HP (Q=0.707) at fc=2000Hz:
    // |H(f)|² = (f/fc)⁴ / ((1−(f/fc)²)² + (f/(fc·Q))²)
    // At 5000Hz (f/fc=2.5): |H| ≈ 0.987 ≈ −0.11 dB.
    // At 100Hz (f/fc=0.05): |H| ≈ 0.0025 ≈ −52 dB.
    // Expected ratio ≈ 52 dB. Threshold 45 dB allows 7 dB margin.
    float ratioDb = 20.0f * std::log10(highRms / std::max(lowRms, 1e-10f));
    REQUIRE(ratioDb > 45.0f);
}

TEST_CASE("Biquad bandpass: passes center, attenuates edges", "[biquad]")
{
    ideath::Biquad bq;
    bq.setBandpass(1000.0f, 2.0f, kSampleRate);

    constexpr int N = 4096;

    // 1000 Hz — center, should pass.
    auto center = makeSine(1000.0f, kSampleRate, N);
    auto centerOut = processBuffer(bq, center);
    float centerRms = rms(centerOut);

    bq.reset();

    // 100 Hz — far below, should be attenuated.
    auto low = makeSine(100.0f, kSampleRate, N);
    auto lowOut = processBuffer(bq, low);
    float lowRms = rms(lowOut);

    bq.reset();

    // 10000 Hz — far above, should be attenuated.
    auto high = makeSine(10000.0f, kSampleRate, N);
    auto highOut = processBuffer(bq, high);
    float highRms = rms(highOut);

    // 2nd-order BPF (constant-skirt, RBJ) at fc=1000Hz, Q=2:
    // Analog prototype: |H(jω)|² = ω² / ((1−ω²)² + (ω/Q)²), ω = f/fc.
    // At center (ω=1): |H| = Q = 2 (+6 dB peak gain).
    // At 100Hz (ω=0.1): |H| ≈ 0.101 (−20 dB).
    // At 10000Hz (ω=10): |H| ≈ 0.101 (−20 dB, symmetric).
    // Ratio = 2/0.101 ≈ 20 = 26 dB. Threshold 20 dB allows 6 dB margin.
    float lowDb = 20.0f * std::log10(centerRms / std::max(lowRms, 1e-10f));
    float highDb = 20.0f * std::log10(centerRms / std::max(highRms, 1e-10f));
    REQUIRE(lowDb > 20.0f);
    REQUIRE(highDb > 20.0f);
}

TEST_CASE("Biquad: setCoefficients works", "[biquad]")
{
    ideath::Biquad bq;
    bq.setCoefficients(0.5f, 0.0f, 0.0f, 0.0f, 0.0f);
    // Should scale input by 0.5.
    REQUIRE_THAT(bq.process(1.0f), WithinAbs(0.5f, 1e-6f));
}

// --- Long-run stability ---

TEST_CASE("Biquad: long-run stability (10 seconds)", "[biquad]")
{
    ideath::Biquad bq;
    bq.setLowpass(1000.0f, 5.0f, kSampleRate);

    // 10 seconds of saw-like input. Tests denormal protection (DC offset
    // 1e-25 in process()) and coefficient stability over 441k samples.
    constexpr int N = 441000;
    for (int i = 0; i < N; ++i)
    {
        float input = (i % 2 == 0) ? 0.5f : -0.5f; // alternating, stresses feedback
        float s = bq.process(input);
        REQUIRE(std::isfinite(s));
    }
}

// --- Parameter boundary behavior ---

TEST_CASE("Biquad: cutoff at minimum (10 Hz) near-silences signal", "[biquad]")
{
    ideath::Biquad bq;
    // sanitizeParams clamps freq to [10, sr*0.45]. At 10 Hz LP, a 440 Hz
    // sine is 44× above cutoff → heavily attenuated.
    bq.setLowpass(10.0f, 0.707f, kSampleRate);

    constexpr int N = 4096;
    auto sig = makeSine(440.0f, kSampleRate, N);
    auto out = processBuffer(bq, sig);
    float outRms = rms(out);

    // |H(440/10)| = |H(44)| ≈ 1/44² ≈ 0.0005 (−66 dB). Signal nearly silent.
    REQUIRE(outRms < 0.01f);
}

TEST_CASE("Biquad: cutoff at Nyquist limit is near-passthrough", "[biquad]")
{
    ideath::Biquad bq;
    // sanitizeParams clamps to sr*0.45 = 19845 Hz. LP at Nyquist: everything passes.
    bq.setLowpass(22050.0f, 0.707f, kSampleRate);

    constexpr int N = 4096;
    auto sig = makeSine(440.0f, kSampleRate, N);
    float inRms = rms(sig);
    auto out = processBuffer(bq, sig);
    float outRms = rms(out);

    // 440 Hz through LP at ~19845 Hz: deep passband, gain ≈ 1.0.
    // Allow 0.5 dB loss for the transient.
    REQUIRE(outRms > inRms * 0.94f);
}

TEST_CASE("Biquad: minimum Q produces flat response", "[biquad]")
{
    ideath::Biquad bq;
    // sanitizeParams clamps Q to ≥0.01. At very low Q the filter is overdamped.
    bq.setLowpass(1000.0f, 0.01f, kSampleRate);

    constexpr int N = 4096;
    auto sig = makeSine(500.0f, kSampleRate, N);
    auto out = processBuffer(bq, sig);

    // At Q=0.01, the filter is extremely overdamped — nearly no resonance.
    // 500 Hz through LP 1000 Hz: in passband, should still pass (attenuated
    // due to overdamping but not silenced). Output must be finite.
    for (auto s : out)
        REQUIRE(std::isfinite(s));
    REQUIRE(rms(out) > 0.01f);
}

// --- Extreme parameter combinations ---

TEST_CASE("Biquad: high Q + high cutoff stays finite", "[biquad]")
{
    ideath::Biquad bq;
    bq.setLowpass(18000.0f, 50.0f, kSampleRate);

    constexpr int N = 44100;
    for (int i = 0; i < N; ++i)
    {
        float input = std::sin(2.0f * static_cast<float>(M_PI) * 440.0f
                               * static_cast<float>(i) / kSampleRate);
        float s = bq.process(input);
        REQUIRE(std::isfinite(s));
    }
}

TEST_CASE("Biquad: high Q + low cutoff stays finite", "[biquad]")
{
    ideath::Biquad bq;
    bq.setLowpass(20.0f, 50.0f, kSampleRate);

    constexpr int N = 44100;
    for (int i = 0; i < N; ++i)
    {
        float input = std::sin(2.0f * static_cast<float>(M_PI) * 440.0f
                               * static_cast<float>(i) / kSampleRate);
        float s = bq.process(input);
        REQUIRE(std::isfinite(s));
    }
}

TEST_CASE("Biquad: rapid coefficient changes stay stable", "[biquad]")
{
    // Simulates per-sample modulation: cutoff sweeps every sample.
    // This stresses the filter state with constantly changing coefficients.
    ideath::Biquad bq;

    constexpr int N = 44100;
    for (int i = 0; i < N; ++i)
    {
        float sweep = 200.0f + 4000.0f * (0.5f + 0.5f * std::sin(
            2.0f * static_cast<float>(M_PI) * 5.0f
            * static_cast<float>(i) / kSampleRate));
        bq.setLowpass(sweep, 5.0f, kSampleRate);

        float input = std::sin(2.0f * static_cast<float>(M_PI) * 440.0f
                               * static_cast<float>(i) / kSampleRate);
        float s = bq.process(input);
        REQUIRE(std::isfinite(s));
    }
}
