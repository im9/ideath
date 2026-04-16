#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Noise.h>
#include <cmath>

using Catch::Matchers::WithinAbs;

// Threshold derivations used throughout this file
// -----------------------------------------------
// Noise::process() is xorshift32 mapped to [-1, 1] via
//     out = state * (2 / (2^32 - 1)) - 1
// where state is a uniform 32-bit value (xorshift32 visits every non-zero
// state exactly once, period = 2^32 − 1). The output distribution is
// uniform on a discrete lattice within (−1, 1]; treat it as continuous
// uniform on [−1, 1] for statistics:
//
//   E[x]   = 0
//   E[x²]  = 1/3
//   σ      = 1/√3 ≈ 0.5774  (this is the RMS)
//   E[x⁴]  = 1/5
//
// Sample statistics from N i.i.d. draws:
//   std of sample mean       = σ / √N
//   std of sample mean-square = √((E[x⁴] − E[x²]²) / N) = √(4/45 · 1/N)
//   std of sample RMS        ≈ (1 / (2·RMS)) · std(mean-square)
//                             = √(4/45) / (2·√3·√N)  ≈ 0.258 / √N
//
// For N=100 000:
//   3σ mean bound ≈ 3 · 0.5774 / √N = 5.47e-3
//   3σ RMS bound  ≈ 3 · 0.258  / √N = 2.45e-3
//
// Float bound on output: state ∈ {1, 2, …, 2^32 − 1}. In float, state
// rounds to multiples of 256 near 2^32, so state·kScale lands in
// [≈2^-31, 2.0]; subtract 1 → strict [−1 + 2^-31, 1]. No slack needed
// on the upper bound.

TEST_CASE("Noise: output is in [-1, 1] range", "[noise]")
{
    ideath::Noise noise;

    // xorshift32 cycles through every non-zero 32-bit state over its
    // 2^32 − 1 period; 100 000 samples exercises ≈1 / 43 000 of the
    // state space, enough to hit values across the full magnitude
    // range. The bound is a hard invariant of the formula, not
    // probabilistic, so equality on the edges is fine.
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

    // 3σ = 5.5e-3 for uniform [-1, 1]. Observed with default seed: 1.3e-3.
    // 0.01 is ~5σ — conservative enough to not flake across compilers,
    // 2× tighter than the old 0.02 bound.
    float mean = static_cast<float>(sum / static_cast<double>(N));
    REQUIRE_THAT(mean, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("Noise: RMS matches uniform distribution", "[noise]")
{
    ideath::Noise noise;
    constexpr int N = 100000;

    double sumSq = 0.0;
    for (int i = 0; i < N; ++i)
    {
        float s = noise.process();
        sumSq += static_cast<double>(s) * static_cast<double>(s);
    }

    // Theoretical RMS = 1/√3 ≈ 0.5774. 3σ statistical bound at N=100 000
    // is ≈ 2.45e-3. Observed: 0.5779, within 5e-4 of theory. Use 0.005
    // to cover 5σ variability — still 100× tighter than old [0.4, 0.7].
    float rmsVal = static_cast<float>(std::sqrt(sumSq / static_cast<double>(N)));
    REQUIRE_THAT(rmsVal, WithinAbs(1.0f / std::sqrt(3.0f), 0.005f));
}

TEST_CASE("Noise: same seed produces same sequence", "[noise]")
{
    // xorshift32 is deterministic: given identical state_ the three
    // xor-shift operations produce identical next states, so output
    // equality is bit-exact.
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

    // reset(seed) assigns state_ = seed literal, so the subsequent call
    // reproduces the first output bit-exact.
    noise.reset(99u);
    REQUIRE(noise.process() == first);
}

TEST_CASE("Noise: different seeds produce different sequences", "[noise]")
{
    // Probability that two xorshift32 streams coincide for 100 consecutive
    // samples starting from different non-zero seeds is vanishingly small
    // (≈ 2^-3200). Detecting a single divergent sample is sufficient.
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

// ---------------------------------------------------------------------------
// Distribution and long-run stability
// ---------------------------------------------------------------------------

TEST_CASE("Noise: uniform distribution across 10 buckets", "[noise]")
{
    // Histogram into 10 equal bins over [-1, 1]. For each bin,
    //   expected count = N/10 = 100 000
    //   σ = √(N · 0.1 · 0.9) = √90 000 ≈ 300
    //   3σ bound = 900
    // Use 1500 (5σ) to absorb finite-seed bias from the single default
    // state without allowing a genuine non-uniformity (e.g. stuck bit)
    // to slip through. Observed span at N=1e6: 99 677 … 100 324.
    ideath::Noise noise;
    constexpr int N = 1000000;
    int buckets[10] = {0};
    for (int i = 0; i < N; ++i)
    {
        float s = noise.process();
        int b = static_cast<int>((s + 1.0f) * 5.0f);
        if (b < 0) b = 0;
        if (b > 9) b = 9;
        ++buckets[b];
    }

    constexpr int expected = N / 10;
    for (int i = 0; i < 10; ++i)
    {
        INFO("bucket " << i << " = " << buckets[i]);
        REQUIRE(std::abs(buckets[i] - expected) < 1500);
    }
}

TEST_CASE("Noise: 10-second stability", "[noise][stability]")
{
    // 10 s × 44.1 kHz = 441 000 process() calls. xorshift32 state remains
    // non-zero (fixed point 0 is excluded from the cycle) for any non-zero
    // seed, so output stays finite and bounded for the full period.
    ideath::Noise noise(0xDEADBEEFu);
    for (int i = 0; i < 441000; ++i)
    {
        float s = noise.process();
        REQUIRE(std::isfinite(s));
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
        // State never collapses to zero — xorshift has no way back to 0
        // once seeded non-zero.
        REQUIRE(noise.getState() != 0u);
    }
}

TEST_CASE("Noise: seed diversity — several seeds produce distinct means", "[noise]")
{
    // Guard against a pathological reset/seed bug where every seed maps
    // to the same downstream state. Computing the mean over 10 000 samples
    // from several unrelated seeds, all means should differ pairwise
    // (matching probability per pair ≈ 10^-300).
    constexpr int N = 10000;
    auto meanFor = [](uint32_t seed) {
        ideath::Noise noise(seed);
        double sum = 0.0;
        for (int i = 0; i < N; ++i)
            sum += noise.process();
        return sum / N;
    };
    uint32_t seeds[] = { 1u, 2u, 0xDEADBEEFu, 0xCAFEBABEu, 0x12345678u };
    double means[5];
    for (int i = 0; i < 5; ++i)
        means[i] = meanFor(seeds[i]);

    for (int i = 0; i < 5; ++i)
        for (int j = i + 1; j < 5; ++j)
            REQUIRE(means[i] != means[j]);
}
