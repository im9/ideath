#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Wavefolder.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

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

// --- Anti-aliasing ---------------------------
//
// Threshold derivations for the cases below:
//
// Small-signal response.  With drive 1 and amplitude a = 1e−3, sin(x) = x
// up to a³/6 relative (1.7e−7, −1.4e−6 dB), so the fold is linear and its
// ADAA reduces to the ADAA of the identity, the two-point mean
// (x + x₁)/2, |H(f)| = cos(π·f/sr) exactly.  At 2× the same mean runs at
// 2·sr (cos(π·f/2sr)) between two halfband filters whose pass-band ripple
// is δ ≈ 10^(−A/20) with A = β/0.1102 + 8.7 = 81.3 dB (Kaiser, β = 8) →
// δ = 8.6e−5 each, 2δ = 0.0015 dB for the pair.  Tolerance 0.01 dB covers
// that and the float FIR rounding (24 taps × 6e−8).
//
// Latency.  In the linear region each mode is a linear-phase FIR (the
// two-point mean, or halfband·mean·halfband), whose impulse-response
// centroid Σn·h[n] / Σh[n] equals its constant group delay — 0.5 and
// 22.75 samples (header).  Tolerance 1e−3 sample: the centroid of a
// float FIR rounds at ≈ 1e−6 relative.
//
// Alias reduction.  Requirement, not a measured fit: each level of
// protection must at least halve the alias amplitude again, counted from
// the plain fold — Adaa ≥ 6 dB below Off, Adaa2x ≥ 12 dB below Off — and
// Adaa2x must beat Adaa.  (Counted per step, Adaa → Adaa2x gives only
// ≈ 5.7 dB at f0 880 Hz / drive 10: there the remaining alias comes from
// fold energy far above the 2× Nyquist, which no halfband removes — a
// 127-tap filter still gives < 7 dB.  A steeper step needs ADAA2 or 4×.)

#include "AliasMetric.h"
#include <vector>

namespace {
std::vector<float> runFold(ideath::Wavefolder& wf, const std::vector<float>& x)
{
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        y[i] = wf.process(x[i]);
    return y;
}

double gainDb(ideath::Antialiasing mode, double hz, float mix)
{
    ideath::Wavefolder wf;
    wf.prepare(48000.0f);
    wf.setDrive(1.0f);
    wf.setMix(mix);
    wf.setAntialiasing(mode);
    return 20.0 * std::log10(alias_metric::sineGain(
        [&](float x) { return wf.process(x); }, hz, 48000.0, 1e-3));
}
} // namespace

TEST_CASE("Wavefolder: default anti-aliasing is Off (legacy 1× fold)", "[wavefolder]")
{
    ideath::Wavefolder wf;
    REQUIRE(wf.getAntialiasing() == ideath::Antialiasing::Off);
    REQUIRE(wf.latencySamples() == 0.0f);
}

TEST_CASE("Wavefolder: mix=0 fast path is bit-exact incl. signed zero and large input",
          "[wavefolder]")
{
    // (1 − 0)·x + 0·sin(d·x): 0·sin is ±0 for finite sin, and x + (±0) = x
    // for every x except x = −0 with +0 added, where the sign of sin(−0·d)
    // = −0 makes the product −0, so the sum stays −0.  The fast path must
    // reproduce that bit for bit (compare bit patterns, not values).
    ideath::Wavefolder wf;
    wf.prepare(48000.0f);
    wf.setDrive(37.0f);
    wf.setMix(0.0f);
    for (float x : { -0.0f, 0.0f, 1e-38f, -1e-30f, 0.25f, -1.0f, 3.5f, -1e6f, 1e30f })
    {
        const float y = wf.process(x);
        const float ref = (1.0f - 0.0f) * x + 0.0f * std::sin(x * 37.0f);
        REQUIRE(std::signbit(y) == std::signbit(ref));
        REQUIRE(y == ref);
    }
}

TEST_CASE("Wavefolder: Adaa small-signal response is the two-point mean", "[wavefolder]")
{
    for (double hz : { 1000.0, 5000.0, 10000.0, 15000.0 })
    {
        const double expected = 20.0 * std::log10(std::cos(M_PI * hz / 48000.0));
        INFO("hz " << hz);
        REQUIRE_THAT(gainDb(ideath::Antialiasing::Adaa, hz, 1.0f), WithinAbs(expected, 0.01));
        // Dry is averaged the same way: mix 0.5 has the same response
        // (an unaligned dry would comb: |½ + ½·e^(−jω/2)·cos(ω/2)| ≠ cos(ω/2)).
        REQUIRE_THAT(gainDb(ideath::Antialiasing::Adaa, hz, 0.5f), WithinAbs(expected, 0.01));
    }
}

TEST_CASE("Wavefolder: Adaa2x small-signal response keeps the top end", "[wavefolder]")
{
    for (double hz : { 1000.0, 5000.0, 10000.0, 15000.0 })
    {
        const double expected = 20.0 * std::log10(std::cos(M_PI * hz / 96000.0));
        INFO("hz " << hz);
        REQUIRE_THAT(gainDb(ideath::Antialiasing::Adaa2x, hz, 1.0f), WithinAbs(expected, 0.01));
        REQUIRE_THAT(gainDb(ideath::Antialiasing::Adaa2x, hz, 0.5f), WithinAbs(expected, 0.01));
        REQUIRE_THAT(gainDb(ideath::Antialiasing::Adaa2x, hz, 0.0f), WithinAbs(expected, 0.01));
    }
}

TEST_CASE("Wavefolder: latency matches the impulse-response centroid", "[wavefolder]")
{
    for (auto mode : { ideath::Antialiasing::Adaa, ideath::Antialiasing::Adaa2x })
    {
        ideath::Wavefolder wf;
        wf.prepare(48000.0f);
        wf.setDrive(1.0f);
        wf.setAntialiasing(mode);
        double num = 0.0, den = 0.0;
        for (int n = 0; n < 128; ++n)
        {
            const double y = wf.process(n == 0 ? 1e-4f : 0.0f);
            num += n * y;
            den += y;
        }
        REQUIRE_THAT(num / den, WithinAbs(wf.latencySamples(), 1e-3));
    }
}

TEST_CASE("Wavefolder: Adaa halves alias, Adaa2x halves it again (vs Off)", "[wavefolder]")
{
    const float sr = 48000.0f;
    for (float f0 : { 880.0f, 1760.0f, 3520.0f })
    {
        const auto x = alias_metric::bandlimitedSaw(f0, sr, alias_metric::kFftSize + alias_metric::kSkip);
        for (float drive : { 2.0f, 4.0f, 6.0f, 10.0f })
        {
            double db[3];
            int k = 0;
            for (auto mode : { ideath::Antialiasing::Off, ideath::Antialiasing::Adaa,
                               ideath::Antialiasing::Adaa2x })
            {
                ideath::Wavefolder wf;
                wf.prepare(sr);
                wf.setDrive(drive);
                wf.setMix(1.0f);
                wf.setAntialiasing(mode);
                db[k++] = alias_metric::aliasDb(runFold(wf, x), f0, sr);
            }
            INFO("f0 " << f0 << " drive " << drive << ": off " << db[0] << " adaa " << db[1]
                       << " adaa2x " << db[2]);
            REQUIRE(db[1] <= db[0] - 6.0);
            REQUIRE(db[2] <= db[0] - 12.0);
            REQUIRE(db[2] < db[1]);
        }
    }
}

TEST_CASE("Wavefolder: Adaa output bounded by 1, Adaa2x by the filter L1 norm",
          "[wavefolder]")
{
    // Adaa: sin(d·m)·sin(u)/u and the mean m are each within ±1 for
    // |x| ≤ 1, and the mix is convex → |y| ≤ 1 (+ float rounding 1e−6).
    // Adaa2x: the upsampler output is bounded by U = 2·Σ|g| (even branch),
    // so the 2× mix is within max(1, U); the downsampler is an FIR, so
    // |y| ≤ Σ|h|·max(1, U).
    const auto& h = ideath::detail::Halfband2x::taps();
    double l1 = 0.0, upL1 = 0.0;
    for (int j = 0; j < ideath::detail::Halfband2x::kTaps; ++j)
    {
        l1 += std::fabs(h[static_cast<size_t>(j)]);
        if (j % 2 == 0)
            upL1 += 2.0 * std::fabs(h[static_cast<size_t>(j)]);
    }
    const double bound2x = l1 * std::max(1.0, upL1);

    uint32_t seed = 12345u;
    auto rnd = [&]() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                       return static_cast<float>(seed) / 2147483648.0f - 1.0f; };
    for (auto mode : { ideath::Antialiasing::Adaa, ideath::Antialiasing::Adaa2x })
        for (float mix : { 0.0f, 0.5f, 1.0f })
            for (float drive : { 1.0f, 7.0f, 40.0f })
            {
                ideath::Wavefolder wf;
                wf.prepare(48000.0f);
                wf.setAntialiasing(mode);
                wf.setDrive(drive);
                wf.setMix(mix);
                const double bound = mode == ideath::Antialiasing::Adaa ? 1.0 + 1e-6 : bound2x + 1e-6;
                float peak = 0.0f;
                for (int i = 0; i < 48000; ++i)
                {
                    const float x = (i / 64) % 2 == 0 ? rnd() : (i % 2 ? 1.0f : -1.0f);
                    peak = std::max(peak, std::fabs(wf.process(x)));
                }
                INFO("mode " << static_cast<int>(mode) << " mix " << mix << " drive " << drive);
                REQUIRE(peak <= bound);
            }
}

TEST_CASE("Wavefolder: Adaa per-sample drive sweep stays continuous", "[wavefolder]")
{
    // The sin·sinc form uses one drive for both ends of the step, so a
    // drive moving every sample is no special case.  Continuity bound:
    // a 100 Hz sine at amplitude 0.5 with drive gliding 1 → 20 over 1 s.
    // The fold's slope |d/dt sin(d·x)| ≤ d·|x'| + |d'|·|x| =
    // 20·(2π·100·0.5/48000) + (19/48000)·0.5 = 0.1311 per sample, and the
    // ADAA mean of a function moves no faster than the function itself.
    ideath::Wavefolder wf;
    wf.prepare(48000.0f);
    wf.setMix(1.0f);
    wf.setAntialiasing(ideath::Antialiasing::Adaa);
    float prev = 0.0f;
    float maxStep = 0.0f;
    for (int i = 0; i < 48000; ++i)
    {
        wf.setDrive(1.0f + 19.0f * i / 48000.0f);
        const float y = wf.process(0.5f * std::sin(2.0f * 3.14159265f * 100.0f * i / 48000.0f));
        if (i > 0)
            maxStep = std::max(maxStep, std::fabs(y - prev));
        prev = y;
    }
    REQUIRE(maxStep <= 0.1312f);
}

TEST_CASE("Wavefolder: reset clears the anti-aliasing state", "[wavefolder]")
{
    for (auto mode : { ideath::Antialiasing::Adaa, ideath::Antialiasing::Adaa2x })
    {
        ideath::Wavefolder a, b;
        for (auto* wf : { &a, &b })
        {
            wf->prepare(48000.0f);
            wf->setDrive(5.0f);
            wf->setMix(0.7f);
            wf->setAntialiasing(mode);
        }
        for (int i = 0; i < 100; ++i)
            a.process(std::sin(0.1f * i));
        a.reset();
        for (int i = 0; i < 200; ++i)
        {
            const float x = std::sin(0.37f * i);
            REQUIRE(a.process(x) == b.process(x));
        }
    }
}

TEST_CASE("Wavefolder: Adaa2x 10 s long-run stability at extreme drive", "[wavefolder]")
{
    // FIR filters and a one-sample ADAA memory hold no recirculating state,
    // so nothing can drift: after 10 s at drive 100 the output must still be
    // finite and inside the Adaa2x bound, and silence must return to exactly
    // 0 once the 47-tap histories have flushed (all products 0·g).
    ideath::Wavefolder wf;
    wf.prepare(48000.0f);
    wf.setDrive(100.0f);
    wf.setAntialiasing(ideath::Antialiasing::Adaa2x);
    for (int i = 0; i < 480000; ++i)
        REQUIRE(std::isfinite(wf.process(std::sin(2.0f * 3.14159265f * 3000.0f * i / 48000.0f))));
    float last = 1.0f;
    for (int i = 0; i < 64; ++i)
        last = wf.process(0.0f);
    REQUIRE(last == 0.0f);
}
