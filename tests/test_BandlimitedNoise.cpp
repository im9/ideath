#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/BandlimitedNoise.h>
#include <ideath/Noise.h>
#include <cmath>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

// Threshold derivations used throughout this file
// -----------------------------------------------
// BandlimitedNoise.process() is:
//     state ← xorshift32(state)               (same stream as Noise)
//     white = state · (2/(2^32−1)) − 1        ∈ [−1, 1]
//     if coef == 1 → output = white (bypass)
//     else         → lp += coef·(white − lp) + 1e-25      (one-pole LP)
//
// Bandwidth is log-mapped to cutoff fc (kMinHz=5, kMaxHz=0.45·fs):
//     fc = exp(log(5) + (log(0.45·fs) − log(5)) · bandwidth)
//     coef = 1 − exp(−2π·fc/fs)
//
// For a one-pole LP driven by white noise with σ²=1/3:
//     Var(y)               = α·σ²/(2−α)
//     Var(y_n − y_{n−1})   = 2α²·σ²/(2−α)
//     hfRMS(α)             = α·σ·√(2/(2−α))
//
// Evaluated at the rates used by the tests:
//   Bandwidth=1 → coef=1   (bypass): hfRMS = σ·√2 ≈ 0.8165 (white)
//   Bandwidth=0.5 → fc ≈ 315 Hz → coef ≈ 0.0439 → hfRMS ≈ 0.0256
//   Bandwidth=0.1 → fc ≈ 11.4 Hz → coef ≈ 1.63e-3 → hfRMS ≈ 9.4e-4
//
// Convex-combination bound: |lp_new| ≤ (1−α)·|lp| + α·|white| + 1e-25
// so |output| ≤ 1.0 strictly for any α ∈ (0, 1] and any white ∈ [−1, 1].

TEST_CASE("BandlimitedNoise: Bandwidth=1 matches plain Noise", "[bnoise]")
{
    // Bandwidth=1 takes the early-return bypass path in process(), so the
    // xorshift32 output reaches the caller verbatim — identical to the
    // Noise primitive seeded the same way. Bit-exact equality.
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

    // Bound is analytical (convex combination + white ∈ [−1, 1]) — no
    // statistical slack needed. The denormal offset (1e-25) sits far
    // below float ULP at any representable output, so it cannot push
    // the output outside [−1, 1].
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
    // hfRMS as defined in the file header is a proxy for post-filter
    // spectral energy: for a one-pole LP driven by white σ²=1/3 it
    // evaluates to α·σ·√(2/(2−α)), so it collapses to 0 as α→0 (very
    // narrow band) and to σ·√2 ≈ 0.8165 at bypass.
    auto highFreqRMS = [](float bw) {
        ideath::BandlimitedNoise bn(0x12345678u);
        bn.prepare(kSampleRate);
        bn.setBandwidth(bw);
        // Skip 4096 samples so the LP isn't biased by its zero initial
        // state — the one-pole settles to within 1 ULP of its steady
        // state in ~1/α samples (at α=1.6e-3 that's ~600 samples).
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

    // Derived values: white ≈ 0.8165, mid ≈ 0.0256, low ≈ 9.4e-4.
    // Observed at this seed: 0.815, 0.0256, 9.4e-4 → consistent with theory
    // to <1%. Statistical 3σ at N=16384 is ≈ hfRMS · √(1/N) ≈ hfRMS × 0.0078.
    // 15% relative tolerance absorbs finite-sample variability plus ~5% from
    // the log-mapping being fc ≈ 315 Hz rather than exactly half-Nyquist.
    REQUIRE_THAT(static_cast<float>(white), WithinAbs(0.8165f, 0.05f));
    REQUIRE_THAT(static_cast<float>(mid),   WithinAbs(0.0256f, 0.005f));
    REQUIRE_THAT(static_cast<float>(low),   WithinAbs(9.4e-4f, 3e-4f));

    // Monotonicity still meaningful as a regression guard — each step
    // is an ≥10× drop in hfRMS, so the ordering cannot flake.
    REQUIRE(mid < white);
    REQUIRE(low < mid);
}

TEST_CASE("BandlimitedNoise: clamps out-of-range bandwidth", "[bnoise]")
{
    ideath::BandlimitedNoise bn;
    bn.prepare(kSampleRate);

    bn.setBandwidth(-3.0f);  // std::clamp → 0 → fc = 5 Hz
    for (int i = 0; i < 1000; ++i)
        REQUIRE(std::isfinite(bn.process()));

    bn.setBandwidth(99.0f);  // std::clamp → 1 → bypass
    for (int i = 0; i < 1000; ++i)
        REQUIRE(std::isfinite(bn.process()));
}

TEST_CASE("BandlimitedNoise: reset restores deterministic stream", "[bnoise]")
{
    // xorshift32 is deterministic and reset(seed) re-initialises state_
    // plus zeroes lpState_, so two instances with the same seed and
    // bandwidth produce bit-identical output.
    ideath::BandlimitedNoise a(7u);
    ideath::BandlimitedNoise b(7u);
    a.prepare(kSampleRate);
    b.prepare(kSampleRate);
    a.setBandwidth(0.3f);
    b.setBandwidth(0.3f);

    for (int i = 0; i < 1000; ++i)
        REQUIRE(a.process() == b.process());
}

// ---------------------------------------------------------------------------
// Long-run stability and extreme coverage
// ---------------------------------------------------------------------------

TEST_CASE("BandlimitedNoise: 10-second stability at extreme bandwidths", "[bnoise][stability]")
{
    // 10 s = 441 000 samples. The LP feedback path carries lp_new =
    // (1−α)·lp + α·white + 1e-25. The 1e-25 DC offset is a denormal
    // guard (per project convention — see `eede258` reference in
    // CLAUDE.md). Over 441 k samples the offset accumulates to at most
    // 1e-25 / α ≈ 6e-22 at α=1.6e-3, well below float ULP at any non-
    // zero lpState.
    for (float bw : { 0.0f, 0.3f, 1.0f })
    {
        ideath::BandlimitedNoise bn(0xBADC0FFEu);
        bn.prepare(kSampleRate);
        bn.setBandwidth(bw);

        double sum = 0.0;
        for (int i = 0; i < 441000; ++i)
        {
            float s = bn.process();
            REQUIRE(std::isfinite(s));
            REQUIRE(s >= -1.0f);
            REQUIRE(s <= 1.0f);
            sum += s;
        }
        // Mean should track the white source (0) within statistical
        // noise plus the denormal offset's DC contribution (negligible).
        // For N=441 000, 3σ of sample mean of white noise ≈ 3·(1/√3)/√N
        // ≈ 2.6e-3. Use 0.01 to cover 5σ plus any residual LP transient.
        float mean = static_cast<float>(sum / 441000.0);
        INFO("bw=" << bw << " mean=" << mean);
        REQUIRE(std::fabs(mean) < 0.01f);
    }
}

TEST_CASE("BandlimitedNoise: LP settling from zero initial state", "[bnoise]")
{
    // Starting from lpState_=0, the step response to a non-zero-mean
    // input would show an exponential rise with time constant 1/α. Since
    // white has zero mean, the LP "settling" is really the output variance
    // approaching its steady state α·σ²/(2−α). Verify that after ~5/α
    // samples the RMS has converged to the theoretical value within 10%.
    //
    // At bw=0.5: α ≈ 0.0439 → settle in ~115 samples; measure starting at
    // 500 (≈22× time constant) to be safe. Theoretical RMS at bw=0.5:
    //   √(α/(2−α))·σ = √(0.0439/1.956) · (1/√3) ≈ 0.1497 · 0.5774 ≈ 0.0864.
    ideath::BandlimitedNoise bn(0x12345678u);
    bn.prepare(kSampleRate);
    bn.setBandwidth(0.5f);

    for (int i = 0; i < 500; ++i)
        bn.process();

    double sumSq = 0.0;
    constexpr int N = 16384;
    for (int i = 0; i < N; ++i)
    {
        float s = bn.process();
        sumSq += static_cast<double>(s) * static_cast<double>(s);
    }
    float rms = static_cast<float>(std::sqrt(sumSq / N));
    // Statistical 3σ of sample RMS at N=16384 ≈ 8e-4. 15% relative
    // tolerance (≈0.013) comfortably covers it plus modeling error in
    // the log-mapping.
    REQUIRE_THAT(rms, WithinAbs(0.0864f, 0.013f));
}
