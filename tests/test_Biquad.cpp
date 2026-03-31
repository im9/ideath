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

    // Low frequency should be louder than high by at least 20 dB.
    float ratioDb = 20.0f * std::log10(lowRms / std::max(highRms, 1e-10f));
    REQUIRE(ratioDb > 20.0f);
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

    float ratioDb = 20.0f * std::log10(highRms / std::max(lowRms, 1e-10f));
    REQUIRE(ratioDb > 20.0f);
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

    REQUIRE(centerRms > lowRms * 3.0f);
    REQUIRE(centerRms > highRms * 3.0f);
}

TEST_CASE("Biquad: setCoefficients works", "[biquad]")
{
    ideath::Biquad bq;
    bq.setCoefficients(0.5f, 0.0f, 0.0f, 0.0f, 0.0f);
    // Should scale input by 0.5.
    REQUIRE_THAT(bq.process(1.0f), WithinAbs(0.5f, 1e-6f));
}
