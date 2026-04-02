#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/SVFilter.h>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

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

static float rms(const std::vector<float>& buf)
{
    double sum = 0.0;
    for (auto s : buf)
        sum += static_cast<double>(s) * static_cast<double>(s);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(buf.size())));
}

static std::vector<float> processBuffer(ideath::SVFilter& filt, const std::vector<float>& input)
{
    std::vector<float> output(input.size());
    for (size_t i = 0; i < input.size(); ++i)
        output[i] = filt.process(input[i]);
    return output;
}

// --- Default / Reset ---

TEST_CASE("SVFilter: default-constructed is valid", "[svfilter]")
{
    ideath::SVFilter filt;
    filt.prepare(kSampleRate);
    // Should not explode.
    float y = filt.process(1.0f);
    REQUIRE(std::isfinite(y));
}

TEST_CASE("SVFilter: reset clears state", "[svfilter]")
{
    ideath::SVFilter filt;
    filt.prepare(kSampleRate);
    filt.setCutoff(500.0f);
    filt.setResonance(0.5f);

    for (int i = 0; i < 200; ++i)
        filt.process(1.0f);

    filt.reset();
    REQUIRE_THAT(filt.process(0.0f), WithinAbs(0.0f, 1e-6f));
}

// --- Lowpass ---

TEST_CASE("SVFilter lowpass: attenuates high frequencies", "[svfilter]")
{
    ideath::SVFilter filt;
    filt.prepare(kSampleRate);
    filt.setCutoff(500.0f);
    filt.setMode(ideath::SVFilter::Mode::Lowpass);

    constexpr int N = 4096;

    auto low = makeSine(100.0f, kSampleRate, N);
    auto lowOut = processBuffer(filt, low);
    float lowRms = rms(lowOut);

    filt.reset();

    auto high = makeSine(5000.0f, kSampleRate, N);
    auto highOut = processBuffer(filt, high);
    float highRms = rms(highOut);

    float ratioDb = 20.0f * std::log10(lowRms / std::max(highRms, 1e-10f));
    REQUIRE(ratioDb > 20.0f);
}

// --- Highpass ---

TEST_CASE("SVFilter highpass: attenuates low frequencies", "[svfilter]")
{
    ideath::SVFilter filt;
    filt.prepare(kSampleRate);
    filt.setCutoff(2000.0f);
    filt.setMode(ideath::SVFilter::Mode::Highpass);

    constexpr int N = 4096;

    auto high = makeSine(5000.0f, kSampleRate, N);
    auto highOut = processBuffer(filt, high);
    float highRms = rms(highOut);

    filt.reset();

    auto low = makeSine(100.0f, kSampleRate, N);
    auto lowOut = processBuffer(filt, low);
    float lowRms = rms(lowOut);

    float ratioDb = 20.0f * std::log10(highRms / std::max(lowRms, 1e-10f));
    REQUIRE(ratioDb > 20.0f);
}

// --- Bandpass ---

TEST_CASE("SVFilter bandpass: passes center, attenuates edges", "[svfilter]")
{
    ideath::SVFilter filt;
    filt.prepare(kSampleRate);
    filt.setCutoff(1000.0f);
    filt.setResonance(0.5f);
    filt.setMode(ideath::SVFilter::Mode::Bandpass);

    constexpr int N = 4096;

    auto center = makeSine(1000.0f, kSampleRate, N);
    auto centerOut = processBuffer(filt, center);
    float centerRms = rms(centerOut);

    filt.reset();

    auto low = makeSine(100.0f, kSampleRate, N);
    auto lowOut = processBuffer(filt, low);
    float lowRms = rms(lowOut);

    filt.reset();

    auto high = makeSine(10000.0f, kSampleRate, N);
    auto highOut = processBuffer(filt, high);
    float highRms = rms(highOut);

    REQUIRE(centerRms > lowRms * 3.0f);
    REQUIRE(centerRms > highRms * 3.0f);
}

// --- Notch ---

TEST_CASE("SVFilter notch: attenuates center, passes edges", "[svfilter]")
{
    ideath::SVFilter filt;
    filt.prepare(kSampleRate);
    filt.setCutoff(1000.0f);
    filt.setResonance(0.7f);
    filt.setMode(ideath::SVFilter::Mode::Notch);

    constexpr int N = 4096;

    auto center = makeSine(1000.0f, kSampleRate, N);
    auto centerOut = processBuffer(filt, center);
    float centerRms = rms(centerOut);

    filt.reset();

    auto low = makeSine(100.0f, kSampleRate, N);
    auto lowOut = processBuffer(filt, low);
    float lowRms = rms(lowOut);

    filt.reset();

    auto high = makeSine(5000.0f, kSampleRate, N);
    auto highOut = processBuffer(filt, high);
    float highRms = rms(highOut);

    REQUIRE(lowRms > centerRms * 2.0f);
    REQUIRE(highRms > centerRms * 2.0f);
}

// --- Multi-output ---

TEST_CASE("SVFilter processMulti: all outputs are finite", "[svfilter]")
{
    ideath::SVFilter filt;
    filt.prepare(kSampleRate);
    filt.setCutoff(1000.0f);
    filt.setResonance(0.5f);

    for (int i = 0; i < 100; ++i)
    {
        float x = std::sin(2.0f * static_cast<float>(M_PI) * 440.0f * static_cast<float>(i) / kSampleRate);
        auto out = filt.processMulti(x);
        REQUIRE(std::isfinite(out.low));
        REQUIRE(std::isfinite(out.high));
        REQUIRE(std::isfinite(out.band));
        REQUIRE(std::isfinite(out.notch));
    }
}

TEST_CASE("SVFilter processMulti: notch == low + high", "[svfilter]")
{
    ideath::SVFilter filt;
    filt.prepare(kSampleRate);
    filt.setCutoff(800.0f);
    filt.setResonance(0.6f);

    for (int i = 0; i < 200; ++i)
    {
        float x = std::sin(2.0f * static_cast<float>(M_PI) * 440.0f * static_cast<float>(i) / kSampleRate);
        auto out = filt.processMulti(x);
        REQUIRE_THAT(out.notch, WithinAbs(out.low + out.high, 1e-5f));
    }
}

// --- Output range ---

TEST_CASE("SVFilter: output stays bounded", "[svfilter]")
{
    ideath::SVFilter filt;
    filt.prepare(kSampleRate);
    filt.setCutoff(1000.0f);
    filt.setResonance(0.9f);
    filt.setMode(ideath::SVFilter::Mode::Lowpass);

    for (int i = 0; i < 4096; ++i)
    {
        float x = std::sin(2.0f * static_cast<float>(M_PI) * 440.0f * static_cast<float>(i) / kSampleRate);
        float y = filt.process(x);
        REQUIRE(std::abs(y) < 10.0f);
    }
}

// --- Modulation stability ---

TEST_CASE("SVFilter: stable under fast cutoff modulation", "[svfilter]")
{
    ideath::SVFilter filt;
    filt.prepare(kSampleRate);
    filt.setResonance(0.8f);
    filt.setMode(ideath::SVFilter::Mode::Lowpass);

    // Sweep cutoff rapidly every sample — should not explode.
    for (int i = 0; i < 4096; ++i)
    {
        float mod = 500.0f + 4000.0f * (0.5f + 0.5f * std::sin(2.0f * static_cast<float>(M_PI) * 10.0f * static_cast<float>(i) / kSampleRate));
        filt.setCutoff(mod);
        float x = std::sin(2.0f * static_cast<float>(M_PI) * 440.0f * static_cast<float>(i) / kSampleRate);
        float y = filt.process(x);
        REQUIRE(std::isfinite(y));
        REQUIRE(std::abs(y) < 10.0f);
    }
}

// --- Parameter clamping ---

TEST_CASE("SVFilter: extreme parameters don't explode", "[svfilter]")
{
    ideath::SVFilter filt;
    filt.prepare(kSampleRate);

    // Very low cutoff.
    filt.setCutoff(1.0f);
    filt.setResonance(0.99f);
    for (int i = 0; i < 100; ++i)
    {
        float y = filt.process(1.0f);
        REQUIRE(std::isfinite(y));
    }

    filt.reset();

    // Very high cutoff.
    filt.setCutoff(20000.0f);
    filt.setResonance(0.0f);
    for (int i = 0; i < 100; ++i)
    {
        float y = filt.process(1.0f);
        REQUIRE(std::isfinite(y));
    }
}
