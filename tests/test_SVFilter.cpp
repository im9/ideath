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

TEST_CASE("SVFilter: default-constructed without prepare() is usable", "[svfilter]")
{
    // API consistency with Biquad, which defaults to passthrough (b0_=1.0f) so
    // process() on a default-constructed instance is meaningful.  SVFilter
    // headers default a1_=a2_=a3_=0, which would make Lowpass output silence
    // for any input — a hidden failure mode if prepare() is forgotten.  Default
    // cutoffHz_=1000 / sampleRate_=44100 / resonance_=0 imply Butterworth LP
    // with DC gain = 1.0, so a constant DC input must converge to ~1.0.
    ideath::SVFilter filt;
    // Intentionally NO prepare() call.
    float y = 0.0f;
    // 2000 samples ≫ 1/(2*pi*1000/44100) ≈ 7 samples time-constant — plenty
    // to converge to DC steady state within 1% of unity.
    for (int i = 0; i < 2000; ++i)
        y = filt.process(1.0f);
    REQUIRE_THAT(y, WithinAbs(1.0f, 0.01f));
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

    // SVF LP, resonance=0 → k=2, Q=1/k=0.5 (overdamped).
    // |H(f)| = 1/sqrt((1−(f/fc)²)² + (f/(fc·Q))²)
    // At 100Hz (f/fc=0.2): |H| ≈ 0.961 ≈ −0.34 dB.
    // At 5000Hz (f/fc=10): |H| ≈ 0.0099 ≈ −40 dB.
    // Expected ratio ≈ 40 dB. Threshold 35 dB allows 5 dB margin.
    float ratioDb = 20.0f * std::log10(lowRms / std::max(highRms, 1e-10f));
    REQUIRE(ratioDb > 35.0f);
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

    // SVF HP, resonance=0 → Q=0.5.
    // At 5000Hz (f/fc=2.5): |H_HP| ≈ 0.862 ≈ −1.3 dB.
    // At 100Hz (f/fc=0.05): |H_HP| ≈ 0.0025 ≈ −52 dB.
    // Expected ratio ≈ 51 dB. Threshold 40 dB allows 11 dB margin
    // (extra for Q=0.5 passband droop at 5000Hz).
    float ratioDb = 20.0f * std::log10(highRms / std::max(lowRms, 1e-10f));
    REQUIRE(ratioDb > 40.0f);
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

    // SVF BP at 1000Hz, resonance=0.5 → k=1, Q=1.
    // |H_BP(f/fc)| = (f/fc)/Q / sqrt((1−(f/fc)²)² + ((f/fc)/Q)²)
    // At center: |H| = 1. At 100Hz (f/fc=0.1): |H| ≈ 0.1 (−20 dB).
    // At 10000Hz (f/fc=10): |H| ≈ 0.1 (−20 dB).
    // Ratio = 20 dB. Threshold 15 dB allows 5 dB margin.
    float lowDb = 20.0f * std::log10(centerRms / std::max(lowRms, 1e-10f));
    float highDb = 20.0f * std::log10(centerRms / std::max(highRms, 1e-10f));
    REQUIRE(lowDb > 15.0f);
    REQUIRE(highDb > 15.0f);
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

    // SVF Notch at 1000Hz, resonance=0.7 → k=0.6, Q=1/0.6≈1.67.
    // Notch at center: |H| → 0 (ideal null). At 100Hz: |H| ≈ 1. At 5000Hz: |H| ≈ 1.
    // Notch depth depends on Q; Q=1.67 gives deep null (>30 dB).
    // Threshold 15 dB is conservative for the ratio edges/center.
    float lowDb = 20.0f * std::log10(lowRms / std::max(centerRms, 1e-10f));
    float highDb = 20.0f * std::log10(highRms / std::max(centerRms, 1e-10f));
    REQUIRE(lowDb > 15.0f);
    REQUIRE(highDb > 15.0f);
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
        // notch is computed as low + high in processMulti(). Re-summing
        // the same float values is bit-identical. Tolerance = float epsilon.
        REQUIRE_THAT(out.notch, WithinAbs(out.low + out.high, 1e-6f));
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
        // Resonance=0.9 → k=0.2, Q=5. Max gain at resonance = Q = 5.
        // Sine at 440Hz is below cutoff 1000Hz; gain ≈ 1.23 (below Q).
        // Bound = Q + 0.5 for transient overshoot.
        REQUIRE(std::abs(y) < 5.5f);
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
        // Resonance=0.8 → k=0.4, Q=2.5. Max gain at resonance = Q.
        // TPT SVF is unconditionally stable under modulation, but stored
        // energy in state variables can briefly exceed steady-state Q.
        // Bound = 2×Q = 5.0 is conservative.
        REQUIRE(std::abs(y) < 5.0f);
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

// --- Long-run stability ---

TEST_CASE("SVFilter: long-run stability (10 seconds)", "[svfilter]")
{
    ideath::SVFilter filt;
    filt.prepare(kSampleRate);
    filt.setCutoff(1000.0f);
    filt.setResonance(0.9f); // Q=5, high resonance stresses denormal protection
    filt.setMode(ideath::SVFilter::Mode::Lowpass);

    // 10 seconds of alternating input. The DC offset (kAntiDenormal = 1e-25)
    // in ic1eq_/ic2eq_ updates prevents denormal accumulation.
    constexpr int N = 441000;
    for (int i = 0; i < N; ++i)
    {
        float input = (i % 2 == 0) ? 0.5f : -0.5f;
        float s = filt.process(input);
        REQUIRE(std::isfinite(s));
    }
}

TEST_CASE("SVFilter: long-run silence doesn't accumulate DC", "[svfilter]")
{
    ideath::SVFilter filt;
    filt.prepare(kSampleRate);
    filt.setCutoff(500.0f);
    filt.setResonance(0.95f);

    // Feed 0.1s of signal then 10s of silence. State should decay to zero,
    // not accumulate denormals or DC from the anti-denormal offset.
    for (int i = 0; i < 4410; ++i)
        filt.process(std::sin(2.0f * static_cast<float>(M_PI) * 440.0f
                              * static_cast<float>(i) / kSampleRate));

    // 10 seconds of silence
    float lastOut = 0.0f;
    for (int i = 0; i < 441000; ++i)
        lastOut = filt.process(0.0f);

    // After 10s of silence, output should be essentially zero.
    // Q=1/0.1=10, decay constant ~ Q/(π·fc) ≈ 0.0064s. After 10s, the
    // filter has rung down by e^(-10/0.0064) ≈ 0. Tolerance for denormal
    // residue from the 1e-25 DC offset.
    REQUIRE(std::fabs(lastOut) < 1e-6f);
}
