#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Portamento.h>
#include <cmath>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

// Threshold derivations used throughout this file
// -----------------------------------------------
// Portamento is a one-pole IIR: current += coef · (target − current).
// setTime(t) sets coef = 1 − exp(−5 / (t · sampleRate)), so after N samples
//     distance_N = target − current_N = distance_0 · (1 − coef)^N
//                 = distance_0 · exp(−5 · N / samples)
// where samples = t · sampleRate. Consequently:
//   * At N = samples (one "glide time"): distance/distance_0 = exp(−5) ≈ 0.0067
//     — i.e. 99.33% of the way to target.
//   * At N = samples (first glide time), current reaches target (within the
//     snap-to-target guard below) when distance < 1e−5.
//
// Snap guard (process()):
//     if (|target − current| < 1e-5) current = target;
// Works cleanly when current is near 0 (small-magnitude ULP), but *fails to
// fire* when converging toward a value near ±1 because the float increment
// coef · distance becomes smaller than the local ULP (≈1.19e−7 at |x|=1)
// before |distance| crosses the 1e-5 threshold. Observed plateau distance
// for target=1, time=0.1 s: ≈ 2.63e-5, stable after ~10k samples.
//
// These derivations determine the tolerance used in each assertion below.

TEST_CASE("Portamento: instant glide when time is 0", "[portamento]")
{
    ideath::Portamento port;
    port.prepare(kSampleRate);
    port.setTime(0.0f);       // coef_ = 1.0 (instant path)
    port.setValue(0.0f);
    port.setTarget(1.0f);

    // coef=1 ⇒ update is current += 1·(1−0) = 1.0 exactly.
    float out = port.process();
    REQUIRE_THAT(out, WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("Portamento: reaches target eventually", "[portamento]")
{
    ideath::Portamento port;
    port.prepare(kSampleRate);
    port.setTime(0.1f); // 100 ms = 4410 samples per glide time
    port.setValue(0.0f);
    port.setTarget(1.0f);

    // 500 ms = 5× glide time. Analytical distance = exp(−25) ≈ 1.4e−11,
    // well below 1e−5. However float precision near current=1.0 (ULP ≈
    // 1.19e−7) prevents the IIR step from reducing distance below about
    // 5e−5 before increment coef·distance rounds to zero. Observed
    // plateau: 2.63e−5. Tolerance 1e−4 absorbs ULP variation across
    // compilers while still being 10× tighter than the old 0.001.
    for (int i = 0; i < 22050; ++i)
        port.process();

    REQUIRE_THAT(port.getValue(), WithinAbs(1.0f, 1e-4f));
}

TEST_CASE("Portamento: glide is gradual", "[portamento]")
{
    ideath::Portamento port;
    port.prepare(kSampleRate);
    port.setTime(0.1f);       // samples = 4410
    port.setValue(0.0f);
    port.setTarget(1.0f);

    // After N=100 samples: distance = exp(−5·100/4410) = exp(−0.1134)
    //                              ≈ 0.8928.
    // ⇒ current ≈ 1 − 0.8928 = 0.1072.
    // Tolerance 1e−3 captures float accumulation error over 100 IIR steps
    // (each step introduces ~1 ULP rounding on a value near 0.1, summing
    // to ~5e−6 worst case — 200× under the tolerance).
    for (int i = 0; i < 100; ++i)
        port.process();

    REQUIRE_THAT(port.getValue(), WithinAbs(0.1072f, 1e-3f));
}

TEST_CASE("Portamento: glides downward too", "[portamento]")
{
    ideath::Portamento port;
    port.prepare(kSampleRate);
    port.setTime(0.05f);      // samples = 2205
    port.setValue(1.0f);
    port.setTarget(0.0f);

    // After N=100 samples: distance_0 = −1, distance = (−1)·exp(−5·100/2205)
    // = −1 · exp(−0.2268) ≈ −0.7980. current = target − distance = 0.798.
    for (int i = 0; i < 100; ++i)
        port.process();

    REQUIRE_THAT(port.getValue(), WithinAbs(0.7980f, 1e-3f));

    // After 22050 more samples (= 10× glide time). Because the target is 0,
    // the 1e−5 snap guard *does* fire (ULP near 0 is << 1e-5), giving
    // current = 0.0 bit-exact.
    for (int i = 0; i < 22050; ++i)
        port.process();

    REQUIRE_THAT(port.getValue(), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("Portamento: setValue jumps immediately", "[portamento]")
{
    ideath::Portamento port;
    port.prepare(kSampleRate);
    port.setTime(1.0f);
    // setValue assigns current_ = target_ = value literal → bit-exact.
    port.setValue(0.5f);

    REQUIRE_THAT(port.getValue(), WithinAbs(0.5f, 1e-6f));
    REQUIRE_THAT(port.getTarget(), WithinAbs(0.5f, 1e-6f));
}

TEST_CASE("Portamento: changing target mid-glide", "[portamento]")
{
    ideath::Portamento port;
    port.prepare(kSampleRate);
    port.setTime(0.1f);       // samples = 4410
    port.setValue(0.0f);
    port.setTarget(1.0f);

    // After 1000 samples: distance = exp(−5·1000/4410) = exp(−1.134)
    //                               ≈ 0.3219 ⇒ current ≈ 0.6781.
    for (int i = 0; i < 1000; ++i)
        port.process();

    float midValue = port.getValue();
    REQUIRE_THAT(midValue, WithinAbs(0.6781f, 1e-3f));

    // Change target to −1. New distance_0 = −1 − 0.6781 = −1.6781.
    // After 44100 samples (= 10× glide time) distance shrinks by factor
    // exp(−50) ≈ 2e−22 analytically, but plateaus near ±ULP around
    // current=−1. Observed plateau: −0.9999737 → distance = 2.63e−5.
    port.setTarget(-1.0f);

    for (int i = 0; i < 44100; ++i)
        port.process();

    REQUIRE_THAT(port.getValue(), WithinAbs(-1.0f, 1e-4f));
}

TEST_CASE("Portamento: reset clears state", "[portamento]")
{
    ideath::Portamento port;
    port.prepare(kSampleRate);
    port.setTime(0.1f);
    port.setValue(0.8f);
    port.setTarget(0.8f);

    // reset() assigns current_ = target_ = coef_ = 0/0/1 literals.
    port.reset();

    REQUIRE_THAT(port.getValue(), WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(port.getTarget(), WithinAbs(0.0f, 1e-6f));
}

// ---------------------------------------------------------------------------
// Long-run stability and extreme parameter coverage
// ---------------------------------------------------------------------------

TEST_CASE("Portamento: monotonic convergence from both sides", "[portamento]")
{
    // The IIR is strictly convergent: sign(distance_{n+1}) == sign(distance_n)
    // and |distance_{n+1}| ≤ |distance_n| (equality only when increment
    // rounds to 0 near the target). Verify this holds for 10 s of process
    // across a broad amplitude range.
    for (float target : { -5.0f, -1.0f, 0.5f, 3.0f, 100.0f })
    {
        ideath::Portamento port;
        port.prepare(kSampleRate);
        port.setTime(0.25f);
        port.setValue(0.0f);
        port.setTarget(target);

        float prevDist = std::fabs(target);
        for (int i = 0; i < 441000; ++i)
        {
            port.process();
            float dist = std::fabs(target - port.getValue());
            REQUIRE(std::isfinite(port.getValue()));
            REQUIRE(dist <= prevDist + 1e-6f);   // never overshoots
            prevDist = dist;
        }
        // Final distance is either 0 (snap fired for |target| small) or
        // bounded by ~5e-5·|target| (ULP plateau at magnitude |target|).
        // Use a generous 1e-3 relative tolerance so the assertion holds
        // for all five target magnitudes uniformly.
        REQUIRE(std::fabs(target - port.getValue())
                <= 1e-3f * std::max(1.0f, std::fabs(target)));
    }
}

TEST_CASE("Portamento: extreme parameter combinations stay bounded", "[portamento]")
{
    // Negative glide time → setTime early-returns with coef_=1 (instant),
    // same as time=0.
    {
        ideath::Portamento port;
        port.prepare(kSampleRate);
        port.setTime(-0.5f);
        port.setValue(0.0f);
        port.setTarget(2.0f);
        REQUIRE_THAT(port.process(), WithinAbs(2.0f, 1e-6f));
    }

    // Very long glide (1000 s). coef = 1 − exp(−5/(1000·44100)) ≈ 1.13e−7.
    // Per-sample step is tiny but still moves current in the right
    // direction without NaN/inf.
    {
        ideath::Portamento port;
        port.prepare(kSampleRate);
        port.setTime(1000.0f);
        port.setValue(0.0f);
        port.setTarget(1.0f);

        for (int i = 0; i < 44100; ++i)
        {
            float v = port.process();
            REQUIRE(std::isfinite(v));
            REQUIRE(v >= 0.0f);
            REQUIRE(v <= 1.0f);
        }
    }

    // Large target magnitude. Per-sample step = coef · distance is
    // proportional to distance, so it scales linearly with target. No
    // absolute bound exists, just verify finiteness and monotonicity.
    {
        ideath::Portamento port;
        port.prepare(kSampleRate);
        port.setTime(0.01f);
        port.setValue(0.0f);
        port.setTarget(1e6f);

        float prev = 0.0f;
        for (int i = 0; i < 10000; ++i)
        {
            float v = port.process();
            REQUIRE(std::isfinite(v));
            REQUIRE(v >= prev - 1e-3f);        // non-decreasing
            REQUIRE(v <= 1e6f + 1.0f);         // never overshoots target
            prev = v;
        }
    }

    // Rapid target flips: setTarget changes on every sample. No allocation,
    // no divergence, output stays inside [−1, 1] envelope.
    {
        ideath::Portamento port;
        port.prepare(kSampleRate);
        port.setTime(0.01f);
        port.setValue(0.0f);

        for (int i = 0; i < 44100; ++i)
        {
            port.setTarget((i & 1) ? 1.0f : -1.0f);
            float v = port.process();
            REQUIRE(std::isfinite(v));
            REQUIRE(v >= -1.0f - 1e-5f);
            REQUIRE(v <= 1.0f + 1e-5f);
        }
    }
}
