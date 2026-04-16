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
