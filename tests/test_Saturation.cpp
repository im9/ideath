#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Saturation.h>
#include <cmath>

using Catch::Matchers::WithinAbs;

// Threshold derivations used throughout this file
// -----------------------------------------------
// tanhDrive(x, d) = std::tanh(x · d). Odd, strictly monotonic, asymptotes
// at ±1. Taylor series at 0:  tanh(u) = u − u³/3 + 2u⁵/15 − 17u⁷/315 + …
// Near-linear error at small u is dominated by u³/3:
//     |tanh(u) − u|  ≈ u³/3        (the 2u⁵/15 term is opposite sign, so u³/3
//                                   is an upper bound on the absolute error)
// At x=0.1, d=1 the truncation error is 3.33e-4; float std::tanh contributes
// another ≤ a few ULP ≈ 1e-8, negligible against the Taylor remainder.
//
// softClip(x) is a piecewise cubic:
//     y = x − x³/3    for |x| ≤ 1
//     y = ±2/3        for |x| > 1
// Output range is exactly [−2/3, 2/3] because the cubic has derivative
// 1 − x² ≥ 0 on [−1, 1] (strictly increasing) and y(±1) = ±2/3. Error
// from linear at x=0.1 is exactly x³/3 = 3.33e-4 (no higher-order terms).
//
// Both functions are stateless; there is no prepare/reset to exercise.

// --- tanhDrive ---------------------------------------------------------

TEST_CASE("Saturation::tanhDrive: zero in → zero out (bit-exact)", "[sat]")
{
    // tanh(0 · d) = tanh(0) = +0.0 in IEEE 754 for any finite d.
    REQUIRE(ideath::Saturation::tanhDrive(0.0f, 1.0f)   == 0.0f);
    REQUIRE(ideath::Saturation::tanhDrive(0.0f, 5.0f)   == 0.0f);
    REQUIRE(ideath::Saturation::tanhDrive(0.0f, 100.0f) == 0.0f);
}

TEST_CASE("Saturation::tanhDrive: near-linear at small input (Taylor bound)", "[sat]")
{
    // |tanh(x) − x| ≤ x³/3 for |x| ≤ 1. At x=0.1: bound = 3.33e-4, observed
    // 3.32e-4. 1.5× slack absorbs ULP-scale libm variance across platforms.
    constexpr float x = 0.1f;
    constexpr float tol = (x * x * x) / 3.0f * 1.5f;   // ≈ 5e-4
    REQUIRE_THAT(ideath::Saturation::tanhDrive(x, 1.0f), WithinAbs(x, tol));
}

TEST_CASE("Saturation::tanhDrive: matches known values of tanh", "[sat]")
{
    // Closed-form values (double precision):
    //   tanh(1) = 0.76159415595..., tanh(3) = 0.99505475368...
    // 1e-6 absorbs float std::tanh rounding (a few ULP).
    REQUIRE_THAT(ideath::Saturation::tanhDrive(1.0f, 1.0f), WithinAbs(0.76159416f, 1e-6f));
    // tanh(0.5·2) = tanh(1) — proves the multiply happens before tanh.
    REQUIRE_THAT(ideath::Saturation::tanhDrive(0.5f, 2.0f), WithinAbs(0.76159416f, 1e-6f));
    REQUIRE_THAT(ideath::Saturation::tanhDrive(1.0f, 3.0f), WithinAbs(0.99505475f, 1e-6f));
}

TEST_CASE("Saturation::tanhDrive: asymptotic saturation", "[sat]")
{
    // tanh asymptotes to ±1. tanh(10) > 1 − 5e-9, so a drive*input of 10
    // saturates past 0.9999 in float. Very large arguments saturate to
    // exactly ±1.0f under float rounding.
    REQUIRE(ideath::Saturation::tanhDrive( 100.0f, 10.0f) <=  1.0f);
    REQUIRE(ideath::Saturation::tanhDrive(-100.0f, 10.0f) >= -1.0f);
    REQUIRE(ideath::Saturation::tanhDrive( 1.0f, 10.0f) >  0.9999f);
    REQUIRE(ideath::Saturation::tanhDrive(-1.0f, 10.0f) < -0.9999f);
}

TEST_CASE("Saturation::tanhDrive: higher drive compresses more", "[sat]")
{
    // Chord (Δout / Δin) over input [0.3, 0.6]:
    //   at d=2: tanh(1.2) − tanh(0.6) ≈ 0.2966
    //   at d=8: tanh(4.8) − tanh(2.4) ≈ 0.0162
    //   ratio  ≈ 0.055
    // Quantified compression: deltaHigh < 0.1·deltaLow catches direction
    // reversals (e.g. a bug that multiplies by drive twice and expands).
    const float base = 0.3f, bump = 0.6f;
    const float deltaLow  = ideath::Saturation::tanhDrive(bump, 2.0f) - ideath::Saturation::tanhDrive(base, 2.0f);
    const float deltaHigh = ideath::Saturation::tanhDrive(bump, 8.0f) - ideath::Saturation::tanhDrive(base, 8.0f);
    REQUIRE(deltaLow  > 0.25f);          // expected ≈ 0.297
    REQUIRE(deltaHigh < 0.05f);          // expected ≈ 0.016
    REQUIRE(deltaHigh < 0.1f * deltaLow);
}

TEST_CASE("Saturation::tanhDrive: monotonic and odd", "[sat]")
{
    // tanh is strictly increasing and odd: tanh(−u) = −tanh(u). Covers
    // sign preservation as a corollary of odd + f(0)=0.
    constexpr float d = 3.0f;
    float prev = ideath::Saturation::tanhDrive(-2.0f, d);
    for (int i = -199; i <= 200; ++i)
    {
        const float x   = static_cast<float>(i) * 0.01f;
        const float cur = ideath::Saturation::tanhDrive(x, d);
        REQUIRE(cur >= prev);                                                      // monotonic
        REQUIRE_THAT(ideath::Saturation::tanhDrive(-x, d), WithinAbs(-cur, 1e-6f)); // odd
        prev = cur;
    }
}

// --- softClip ----------------------------------------------------------

TEST_CASE("Saturation::softClip: zero in → zero out (bit-exact)", "[sat]")
{
    // y(0) = 0 − 0³/3 = 0 exactly.
    REQUIRE(ideath::Saturation::softClip(0.0f) == 0.0f);
}

TEST_CASE("Saturation::softClip: output range is [-2/3, 2/3]", "[sat]")
{
    // Cubic y = x − x³/3 has y'(x) = 1 − x² ≥ 0 on [−1, 1] (monotonic),
    // with y(±1) = ±2/3. For |x| > 1 the implementation clamps to ±2/3.
    // The tight global range is therefore [−2/3, 2/3], not [−1, 1];
    // asserting ≤ 2/3 catches a regression that forgets to subtract x³/3.
    constexpr float kCeil = 2.0f / 3.0f;
    for (int i = -50; i <= 50; ++i)
    {
        const float x = static_cast<float>(i) * 0.1f;
        const float y = ideath::Saturation::softClip(x);
        REQUIRE(y >= -kCeil);
        REQUIRE(y <=  kCeil);
    }
}

TEST_CASE("Saturation::softClip: matches closed-form cubic for |x| ≤ 1", "[sat]")
{
    // y = x − x³/3 is polynomial — float evaluation is exact up to
    // a few ULP of the two multiplies and subtract. 1e-6 is conservative.
    for (float x : { -0.9f, -0.5f, -0.1f, 0.1f, 0.5f, 0.9f })
    {
        const float expected = x - (x * x * x) / 3.0f;
        REQUIRE_THAT(ideath::Saturation::softClip(x), WithinAbs(expected, 1e-6f));
    }
}

TEST_CASE("Saturation::softClip: clamps to ±2/3 for |x| > 1 (bit-exact)", "[sat]")
{
    // Implementation returns the literal 2.0f/3.0f on either branch.
    constexpr float kCeil = 2.0f / 3.0f;
    REQUIRE(ideath::Saturation::softClip(  1.01f) ==  kCeil);
    REQUIRE(ideath::Saturation::softClip(100.00f) ==  kCeil);
    REQUIRE(ideath::Saturation::softClip( -1.01f) == -kCeil);
    REQUIRE(ideath::Saturation::softClip(-100.0f) == -kCeil);
}

TEST_CASE("Saturation::softClip: monotonic on [-1, 1]", "[sat]")
{
    // Derivative 1 − x² ≥ 0 on [−1, 1] (equality only at endpoints), so
    // strictly increasing on the open interval and non-decreasing on the
    // closed interval. Step 0.01 near |x|=0.99 yields Δy ≈ 0.02·0.01 = 2e-4,
    // well above float ULP at y≈2/3 (~4e-8) so ≥ is safe.
    float prev = ideath::Saturation::softClip(-1.0f);
    for (int i = -99; i <= 100; ++i)
    {
        const float x   = static_cast<float>(i) * 0.01f;
        const float cur = ideath::Saturation::softClip(x);
        REQUIRE(cur >= prev);
        prev = cur;
    }
}

// --- AntialiasedTanh -----------------------
//
// Threshold derivations: same as the Wavefolder anti-aliasing block.
// Small signal (drive 1, amplitude 1e−3): tanh(x) = x up to x³/3 relative
// (3.3e−7, −2.9e−6 dB) → the ADAA is the two-point mean, |H| = cos(π·f/sr)
// (Adaa) or cos(π·f/2sr) between halfbands with 2δ = 0.0015 dB ripple
// (Adaa2x).  Tolerance 0.01 dB.  Alias reduction: Adaa ≥ 6 dB below Off,
// Adaa2x ≥ 12 dB below Off and below Adaa — each level at least halves the
// alias amplitude again (requirement, not a fit; see test_Wavefolder.cpp).
//
// Mean-value bound: for monotonic tanh, the mean of tanh(d·t) over
// [x₁, x] lies between tanh(d·x₁) and tanh(d·x).  The double-precision
// quotient errs by ≤ 2·ε_d·|d·x| / (d·|Δ|) ≤ 4.4e−16·|x| / 1e−5 (Δ ≥ 1e−5
// on that branch) — negligible; the midpoint fallback errs by
// O(d²·Δ²/24) ≤ 4e−8 at d = 10, Δ < 1e−5.  Float rounding of the
// result: 1.2e−7.  Tolerance 1e−6.

#include "AliasMetric.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace {
double tanhGainDb(ideath::Antialiasing mode, double hz)
{
    ideath::Saturation::AntialiasedTanh t;
    t.prepare(48000.0f);
    t.setDrive(1.0f);
    t.setAntialiasing(mode);
    return 20.0 * std::log10(alias_metric::sineGain(
        [&](float x) { return t.process(x); }, hz, 48000.0, 1e-3));
}
} // namespace

TEST_CASE("Saturation::AntialiasedTanh: Off is bit-exact with tanhDrive", "[sat]")
{
    ideath::Saturation::AntialiasedTanh t;
    t.prepare(48000.0f);
    t.setDrive(4.0f);
    t.setAntialiasing(ideath::Antialiasing::Off);
    REQUIRE(t.latencySamples() == 0.0f);
    for (int i = 0; i < 1000; ++i)
    {
        const float x = std::sin(0.013f * i) * 1.3f;
        REQUIRE(t.process(x) == ideath::Saturation::tanhDrive(x, 4.0f));
    }
}

TEST_CASE("Saturation::AntialiasedTanh: default mode is Adaa", "[sat]")
{
    ideath::Saturation::AntialiasedTanh t;
    REQUIRE(t.getAntialiasing() == ideath::Antialiasing::Adaa);
    REQUIRE(t.latencySamples() == 0.5f);
}

TEST_CASE("Saturation::AntialiasedTanh: constant input gives tanh exactly", "[sat]")
{
    // x = x₁ takes the midpoint branch: tanh(d·(x + x)/2) = tanh(d·x)
    // bit for bit ((x + x)/2 = x exactly in float below overflow).
    ideath::Saturation::AntialiasedTanh t;
    t.prepare(48000.0f);
    t.setDrive(3.0f);
    for (float x : { 0.7f, -0.2f, 2.5f })
    {
        t.process(x);
        REQUIRE(t.process(x) == ideath::Saturation::tanhDrive(x, 3.0f));
    }
}

TEST_CASE("Saturation::AntialiasedTanh: Adaa output is the mean between endpoint values",
          "[sat]")
{
    uint32_t seed = 777u;
    auto rnd = [&]() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                       return static_cast<float>(seed) / 2147483648.0f - 1.0f; };
    for (float drive : { 1.0f, 3.0f, 10.0f })
    {
        ideath::Saturation::AntialiasedTanh t;
        t.prepare(48000.0f);
        t.setDrive(drive);
        float xp = 0.0f;
        for (int i = 0; i < 20000; ++i)
        {
            // Mix large jumps, tiny steps (ill-conditioned branch edge) and
            // repeats.
            float x;
            switch (i % 4)
            {
                case 0: x = 2.0f * rnd(); break;
                case 1: x = xp + 1e-6f * rnd(); break;
                case 2: x = xp + 2e-5f * rnd(); break;
                default: x = xp; break;
            }
            const float y = t.process(x);
            const float a = std::tanh(drive * std::min(x, xp));
            const float b = std::tanh(drive * std::max(x, xp));
            REQUIRE(y >= a - 1e-6f);
            REQUIRE(y <= b + 1e-6f);
            xp = x;
        }
    }
}

TEST_CASE("Saturation::AntialiasedTanh: small-signal response", "[sat]")
{
    for (double hz : { 1000.0, 5000.0, 10000.0, 15000.0 })
    {
        INFO("hz " << hz);
        REQUIRE_THAT(tanhGainDb(ideath::Antialiasing::Adaa, hz),
                     WithinAbs(20.0 * std::log10(std::cos(M_PI * hz / 48000.0)), 0.01));
        REQUIRE_THAT(tanhGainDb(ideath::Antialiasing::Adaa2x, hz),
                     WithinAbs(20.0 * std::log10(std::cos(M_PI * hz / 96000.0)), 0.01));
    }
}

TEST_CASE("Saturation::AntialiasedTanh: Adaa halves alias, Adaa2x halves it again (vs Off)", "[sat]")
{
    const float sr = 48000.0f;
    for (float f0 : { 880.0f, 3520.0f })
    {
        const auto x = alias_metric::bandlimitedSaw(f0, sr, alias_metric::kFftSize + alias_metric::kSkip);
        for (float drive : { 3.0f, 10.0f })
        {
            double db[3];
            int k = 0;
            for (auto mode : { ideath::Antialiasing::Off, ideath::Antialiasing::Adaa,
                               ideath::Antialiasing::Adaa2x })
            {
                ideath::Saturation::AntialiasedTanh t;
                t.prepare(sr);
                t.setDrive(drive);
                t.setAntialiasing(mode);
                std::vector<float> y(x.size());
                for (size_t i = 0; i < x.size(); ++i)
                    y[i] = t.process(x[i]);
                db[k++] = alias_metric::aliasDb(y, f0, sr);
            }
            INFO("f0 " << f0 << " drive " << drive << ": off " << db[0] << " adaa " << db[1]
                       << " adaa2x " << db[2]);
            REQUIRE(db[1] <= db[0] - 6.0);
            REQUIRE(db[2] <= db[0] - 12.0);
            REQUIRE(db[2] < db[1]);
        }
    }
}

TEST_CASE("Saturation::AntialiasedTanh: drive clamp, reset, extreme input", "[sat]")
{
    ideath::Saturation::AntialiasedTanh t;
    t.prepare(48000.0f);
    t.setDrive(0.0f); // → 1
    t.process(0.5f);
    REQUIRE(t.process(0.5f) == ideath::Saturation::tanhDrive(0.5f, 1.0f));

    // Huge inputs: log-cosh in the |u| + log1p(e^−2|u|) − ln 2 form cannot
    // overflow; the mean stays within ±1 (+ rounding).
    t.setDrive(100.0f);
    for (float x : { 1e6f, -1e6f, 3e7f, 0.0f, -3e7f })
    {
        const float y = t.process(x);
        REQUIRE(std::isfinite(y));
        REQUIRE(std::fabs(y) <= 1.0f + 1e-6f);
    }

    ideath::Saturation::AntialiasedTanh fresh;
    fresh.prepare(48000.0f);
    fresh.setDrive(100.0f);
    t.reset();
    for (int i = 0; i < 100; ++i)
    {
        const float x = std::sin(0.21f * i);
        REQUIRE(t.process(x) == fresh.process(x));
    }
}
