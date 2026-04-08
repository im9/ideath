#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/BandlimitedNoise.h>
#include <ideath/Noise.h>
#include <cmath>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

TEST_CASE("BandlimitedNoise: Bandwidth=1 matches plain Noise", "[bnoise]")
{
    ideath::Noise reference(0xCAFEBABEu);
    ideath::BandlimitedNoise bn(0xCAFEBABEu);
    bn.prepare(kSampleRate);
    bn.setBandwidth(1.0f);

    for (int i = 0; i < 10000; ++i)
        REQUIRE(bn.process() == reference.process());
}

TEST_CASE("BandlimitedNoise: output stays in [-1, 1]", "[bnoise]")
{
    ideath::BandlimitedNoise bn;
    bn.prepare(kSampleRate);

    for (float bw : { 0.0f, 0.1f, 0.3f, 0.5f, 0.7f, 0.9f, 1.0f })
    {
        bn.reset();
        bn.setBandwidth(bw);
        for (int i = 0; i < 5000; ++i)
        {
            float s = bn.process();
            REQUIRE(std::isfinite(s));
            REQUIRE(s >= -1.0f);
            REQUIRE(s <= 1.0f);
        }
    }
}

TEST_CASE("BandlimitedNoise: lower bandwidth reduces high-frequency energy", "[bnoise]")
{
    auto highFreqRMS = [](float bw) {
        ideath::BandlimitedNoise bn(0x12345678u);
        bn.prepare(kSampleRate);
        bn.setBandwidth(bw);
        // Skip the warm-up so the LP isn't biased by its zero initial state.
        for (int i = 0; i < 4096; ++i)
            bn.process();

        double sumSq = 0.0;
        constexpr int N = 16384;
        float prev = bn.process();
        for (int i = 0; i < N; ++i)
        {
            float s = bn.process();
            const double diff = static_cast<double>(s - prev);
            sumSq += diff * diff;
            prev = s;
        }
        return std::sqrt(sumSq / N);
    };

    const double white = highFreqRMS(1.0f);
    const double mid   = highFreqRMS(0.5f);
    const double low   = highFreqRMS(0.1f);

    INFO("white=" << white << " mid=" << mid << " low=" << low);
    REQUIRE(mid < white);
    REQUIRE(low < mid);
}

TEST_CASE("BandlimitedNoise: clamps out-of-range bandwidth", "[bnoise]")
{
    ideath::BandlimitedNoise bn;
    bn.prepare(kSampleRate);

    bn.setBandwidth(-3.0f);  // → 0
    for (int i = 0; i < 1000; ++i)
        REQUIRE(std::isfinite(bn.process()));

    bn.setBandwidth(99.0f);  // → 1
    for (int i = 0; i < 1000; ++i)
        REQUIRE(std::isfinite(bn.process()));
}

TEST_CASE("BandlimitedNoise: reset restores deterministic stream", "[bnoise]")
{
    ideath::BandlimitedNoise a(7u);
    ideath::BandlimitedNoise b(7u);
    a.prepare(kSampleRate);
    b.prepare(kSampleRate);
    a.setBandwidth(0.3f);
    b.setBandwidth(0.3f);

    for (int i = 0; i < 1000; ++i)
        REQUIRE(a.process() == b.process());
}
