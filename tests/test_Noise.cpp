#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Noise.h>
#include <cmath>

using Catch::Matchers::WithinAbs;

TEST_CASE("Noise: output is in [-1, 1] range", "[noise]")
{
    ideath::Noise noise;

    for (int i = 0; i < 100000; ++i)
    {
        float s = noise.process();
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
    }
}

TEST_CASE("Noise: mean is approximately zero", "[noise]")
{
    ideath::Noise noise;
    constexpr int N = 100000;

    double sum = 0.0;
    for (int i = 0; i < N; ++i)
        sum += static_cast<double>(noise.process());

    float mean = static_cast<float>(sum / static_cast<double>(N));
    REQUIRE_THAT(mean, WithinAbs(0.0f, 0.02f));
}

TEST_CASE("Noise: RMS is reasonable (white noise ~0.577)", "[noise]")
{
    ideath::Noise noise;
    constexpr int N = 100000;

    double sumSq = 0.0;
    for (int i = 0; i < N; ++i)
    {
        float s = noise.process();
        sumSq += static_cast<double>(s) * static_cast<double>(s);
    }

    float rmsVal = static_cast<float>(std::sqrt(sumSq / static_cast<double>(N)));
    // Uniform [-1,1] has RMS = 1/sqrt(3) ≈ 0.577.
    REQUIRE(rmsVal > 0.4f);
    REQUIRE(rmsVal < 0.7f);
}

TEST_CASE("Noise: same seed produces same sequence", "[noise]")
{
    ideath::Noise a(42u);
    ideath::Noise b(42u);

    for (int i = 0; i < 1000; ++i)
        REQUIRE(a.process() == b.process());
}

TEST_CASE("Noise: reset restores deterministic sequence", "[noise]")
{
    ideath::Noise noise(99u);
    float first = noise.process();
    for (int i = 0; i < 500; ++i)
        noise.process();

    noise.reset(99u);
    REQUIRE(noise.process() == first);
}

TEST_CASE("Noise: different seeds produce different sequences", "[noise]")
{
    ideath::Noise a(1u);
    ideath::Noise b(2u);

    bool allSame = true;
    for (int i = 0; i < 100; ++i)
    {
        if (a.process() != b.process())
        {
            allSame = false;
            break;
        }
    }
    REQUIRE_FALSE(allSame);
}
