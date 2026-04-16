#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Wavefolder.h>
#include <cmath>

using Catch::Matchers::WithinAbs;

// Threshold derivations used throughout this file
// -----------------------------------------------
// Wavefolder::process(x) = (1 − mix) · x + mix · sin(drive · x)
//   drive clamped to ≥ 1 (no upper bound — sin is inherently bounded).
//   mix   clamped to [0, 1].
//
// Near-linear at small u: sin(u) = u − u³/6 + u⁵/120 − …, so for drive=1:
//     |sin(x) − x|  ≤  x³/6
//   (higher-order terms have alternating sign so the cubic term is an
//   upper bound on the absolute error). At x=0.1 the bound is 1.67e-4;
//   observed 1.666e-4. 1.5× slack absorbs libm ULP variance.
//
// Output bound: for |x| ≤ 1 and any (mix, drive),
//     |(1 − mix)·x + mix·sin(drive·x)|  ≤  (1 − mix)·|x| + mix·1  ≤  1
//   since sin is bounded by 1 and the coefficients are a convex
//   combination. The test therefore restricts |input| ≤ 1 (consistent
//   with the CLAUDE.md output ceiling of ±1 for Wavefolder).
//
// Fold structure at drive=d: extrema of sin(d·x) occur at d·x = π/2 + kπ,
//   i.e. x = (2k+1)π/(2d). At d=8, x ∈ [0, 1] contains extrema at
//   ≈ 0.196, 0.589, 0.982 — one complete descent/ascent half-cycle plus
//   a partial one. Scanning x = 0.05, 0.10, …, 1.00, the decreasing band
//   [0.20, 0.60] gives 8 strictly-decreasing transitions (verified
//   against std::sin reference).
//
// reset() is a no-op (Wavefolder is stateless); the stateless test
// exercises that pure-function property explicitly.

TEST_CASE("Wavefolder: zero in → zero out (bit-exact)", "[wavefolder]")
{
    // sin(0·drive) = 0 and (1−mix)·0 + mix·0 = 0 exactly for any mix, drive.
    ideath::Wavefolder wf;
    wf.prepare(44100.0f);
    REQUIRE(wf.process(0.0f) == 0.0f);

    wf.setDrive(10.0f);
    wf.setMix(0.7f);
    REQUIRE(wf.process(0.0f) == 0.0f);
}

TEST_CASE("Wavefolder: drive=1 near-linear at small input (Taylor bound)", "[wavefolder]")
{
    // |sin(x) − x| ≤ x³/6. At x=0.1 bound = 1.67e-4; 1.5× slack covers
    // libm ULP variance across platforms.
    ideath::Wavefolder wf;
    wf.prepare(44100.0f);
    wf.setDrive(1.0f);

    constexpr float x   = 0.1f;
    constexpr float tol = (x * x * x) / 6.0f * 1.5f; // ≈ 2.5e-4
    REQUIRE_THAT(wf.process(x), WithinAbs(x, tol));
}

TEST_CASE("Wavefolder: output bounded to [-1, 1] for |input| ≤ 1", "[wavefolder]")
{
    // Hard bound from convex combination: |(1−mix)·x + mix·sin(d·x)| ≤ 1
    // whenever |x| ≤ 1, for any (mix, d). Scan input × a grid of params.
    ideath::Wavefolder wf;
    wf.prepare(44100.0f);

    for (float drive : { 1.0f, 3.0f, 10.0f, 50.0f })
    {
        wf.setDrive(drive);
        for (float mix : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            wf.setMix(mix);
            for (int i = -100; i <= 100; ++i)
            {
                const float x = static_cast<float>(i) / 100.0f;
                const float y = wf.process(x);
                INFO("drive=" << drive << " mix=" << mix << " x=" << x);
                REQUIRE(std::isfinite(y));
                REQUIRE(y >= -1.0f);
                REQUIRE(y <=  1.0f);
            }
        }
    }
}

TEST_CASE("Wavefolder: drive=1 is monotonic on [-1, 1]", "[wavefolder]")
{
    // sin is monotonically increasing on [-π/2, π/2], and |1·x| ≤ 1 < π/2.
    // Output = (1−mix)·x + mix·sin(x) is a convex combination of two
    // non-decreasing functions, hence non-decreasing. So drive=1 produces
    // no folds for any mix.
    ideath::Wavefolder wf;
    wf.prepare(44100.0f);
    wf.setDrive(1.0f);

    for (float mix : { 0.0f, 0.5f, 1.0f })
    {
        wf.setMix(mix);
        float prev = wf.process(-1.0f);
        for (int i = -99; i <= 100; ++i)
        {
            const float x   = static_cast<float>(i) / 100.0f;
            const float cur = wf.process(x);
            REQUIRE(cur >= prev);
            prev = cur;
        }
    }
}

TEST_CASE("Wavefolder: drive=8 produces expected fold count", "[wavefolder]")
{
    // sin(8x) has extrema at x = (2k+1)π/16. On x ∈ [0, 1]:
    //   ↑ [0, π/16]         (≈0.196) — increasing
    //   ↓ [π/16, 3π/16]     (≈0.589) — decreasing   (1st fold)
    //   ↑ [3π/16, 5π/16]    (≈0.982) — increasing
    //   (tiny tail past 0.982 — no sample lands there at step 0.05)
    // With step 0.05 the decreasing band [0.20, 0.60] spans 8 strictly
    // decreasing transitions (verified analytically against std::sin).
    // Tight range [7, 9] tolerates ±1 ULP boundary effects.
    ideath::Wavefolder wf;
    wf.prepare(44100.0f);
    wf.setDrive(8.0f);
    wf.setMix(1.0f);

    int decreases = 0;
    float prev = wf.process(0.0f);
    for (int i = 1; i <= 20; ++i)
    {
        const float x   = static_cast<float>(i) * 0.05f;
        const float cur = wf.process(x);
        if (cur < prev) ++decreases;
        prev = cur;
    }
    REQUIRE(decreases >= 7);
    REQUIRE(decreases <= 9);
}

TEST_CASE("Wavefolder: odd symmetry across mix values", "[wavefolder]")
{
    // sin(−u) = −sin(u) and (1−mix)·(−x) = −(1−mix)·x, so the full
    // expression is odd in x for any (mix, drive). 1e-6 covers libm
    // rounding of std::sin on positive vs negative arguments (typically
    // bit-exact but ≤ a few ULP across implementations).
    ideath::Wavefolder wf;
    wf.prepare(44100.0f);
    wf.setDrive(3.0f);

    for (float mix : { 0.0f, 0.5f, 1.0f })
    {
        wf.setMix(mix);
        for (float x : { 0.1f, 0.3f, 0.5f, 0.7f, 0.9f })
        {
            const float pos = wf.process( x);
            const float neg = wf.process(-x);
            REQUIRE_THAT(pos, WithinAbs(-neg, 1e-6f));
        }
    }
}

TEST_CASE("Wavefolder: mix=0 is bit-exact dry", "[wavefolder]")
{
    // (1−0)·x + 0·sin(…) = 1·x + 0 = x exactly. The 0·sin(…) term
    // resolves to +0.0 in IEEE 754 for any finite sin result.
    ideath::Wavefolder wf;
    wf.prepare(44100.0f);
    wf.setDrive(5.0f);
    wf.setMix(0.0f);
    for (float x : { -0.9f, -0.3f, 0.0f, 0.4f, 0.7f })
        REQUIRE(wf.process(x) == x);
}

TEST_CASE("Wavefolder: mix=1 matches std::sin(drive·x)", "[wavefolder]")
{
    // (1−1)·x + 1·sin(d·x) = 0 + sin(d·x) = sin(d·x). Computation is
    // bit-identical if the compiler performs the same std::sin call,
    // but a 1e-6 tolerance protects against platform-level ULP variance.
    ideath::Wavefolder wf;
    wf.prepare(44100.0f);
    wf.setDrive(3.0f);
    wf.setMix(1.0f);
    for (float x : { -0.9f, -0.3f, 0.4f, 0.7f })
    {
        const float expected = std::sin(x * 3.0f);
        REQUIRE_THAT(wf.process(x), WithinAbs(expected, 1e-6f));
    }
}

TEST_CASE("Wavefolder: mix=0.5 blends dry and wet per formula", "[wavefolder]")
{
    // Closed-form: 0.5·x + 0.5·sin(3x). No transcendental involved in
    // the blend itself — 1e-6 tolerance covers the sin ULP.
    ideath::Wavefolder wf;
    wf.prepare(44100.0f);
    wf.setDrive(3.0f);
    wf.setMix(0.5f);

    const float input    = 0.5f;
    const float expected = 0.5f * input + 0.5f * std::sin(input * 3.0f);
    REQUIRE_THAT(wf.process(input), WithinAbs(expected, 1e-6f));
}

TEST_CASE("Wavefolder: mix clamping to [0, 1]", "[wavefolder]")
{
    // setMix(−0.5) → 0 (fully dry). setMix(1.5) → 1 (fully wet).
    // Verify by comparing against the canonical dry/wet output.
    ideath::Wavefolder wf;
    wf.prepare(44100.0f);
    wf.setDrive(3.0f);
    const float x = 0.6f;

    wf.setMix(-0.5f);
    REQUIRE(wf.process(x) == x);                                      // clamped to 0

    wf.setMix(2.0f);
    REQUIRE_THAT(wf.process(x), WithinAbs(std::sin(x * 3.0f), 1e-6f)); // clamped to 1
}

TEST_CASE("Wavefolder: drive clamping to >= 1", "[wavefolder]")
{
    // setDrive(≤ 1) → drive = 1. Output at mix=1 (default) is sin(x·1).
    ideath::Wavefolder wf;
    wf.prepare(44100.0f);

    wf.setDrive(0.0f);
    REQUIRE_THAT(wf.process(0.5f), WithinAbs(std::sin(0.5f), 1e-6f));

    wf.setDrive(-10.0f);
    REQUIRE_THAT(wf.process(0.5f), WithinAbs(std::sin(0.5f), 1e-6f));
}

TEST_CASE("Wavefolder: stateless — process is pure across reset and intervening calls", "[wavefolder]")
{
    // Wavefolder has no internal state, so repeated calls with the same
    // input must return bit-identical output, including across reset().
    ideath::Wavefolder wf;
    wf.prepare(44100.0f);
    wf.setDrive(5.0f);
    wf.setMix(0.7f);

    const float input = 0.42f;
    const float first = wf.process(input);

    wf.process(-0.3f);
    wf.process( 0.9f);
    wf.process( 0.0f);
    REQUIRE(wf.process(input) == first);

    wf.reset();
    REQUIRE(wf.process(input) == first);
}
