#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/LowPassGate.h>
#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace ideath;

static constexpr float kSR = 44100.0f;
static constexpr float kPi = 3.14159265358979323846f;

static float goertzelMagnitude(const std::vector<float>& buf, float freq, float sr)
{
    const int N = static_cast<int>(buf.size());
    const float w = 2.0f * kPi * freq / sr;
    const float coeff = 2.0f * std::cos(w);
    float s1 = 0.0f, s2 = 0.0f;
    for (int n = 0; n < N; ++n)
    {
        const float s0 = buf[n] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const float real = s1 - s2 * std::cos(w);
    const float imag = s2 * std::sin(w);
    return std::sqrt(real * real + imag * imag) * 2.0f / static_cast<float>(N);
}

static float peakAbs(const std::vector<float>& buf)
{
    float m = 0.0f;
    for (float v : buf) m = std::max(m, std::fabs(v));
    return m;
}

// Carrier helper: a single sample of a 440 Hz sine.
static float sine440(int n, float sr)
{
    return std::sin(2.0f * kPi * 440.0f * static_cast<float>(n) / sr);
}

// --- LowPassGate: silent before trigger ----------------------------------

TEST_CASE("LowPassGate: silent before trigger (regardless of carrier)", "[lpg]")
{
    LowPassGate g;
    g.prepare(kSR);
    g.setBrightness(1.0f);
    g.setDamping(0.5f);
    // Default state: envelope idle.  VCA = 0 → output identically zero
    // for any carrier input.  Bit-exact: the VCA multiplies a zero scalar
    // against the filter output (which itself starts at zero).
    for (int n = 0; n < 1000; ++n)
        REQUIRE(g.process(sine440(n, kSR)) == 0.0f);
}

// --- Reset / state ------------------------------------------------------

TEST_CASE("LowPassGate: reset clears envelope and filter state", "[lpg]")
{
    LowPassGate g;
    g.prepare(kSR);
    g.setBrightness(1.0f);
    g.setDamping(0.2f);
    g.trigger(1.0f);
    for (int n = 0; n < 1000; ++n) (void)g.process(sine440(n, kSR));

    g.reset();
    // After reset, envelope is exactly 0 and the filter memory is zeroed.
    // Carrier feed → filter outputs some non-zero value internally, but
    // VCA multiplies by env=0 → output = 0 bit-exact.
    for (int n = 0; n < 1000; ++n)
        REQUIRE(g.process(sine440(n, kSR)) == 0.0f);
}

// --- Envelope shape: fast attack, slow exponential fall ------------------

TEST_CASE("LowPassGate: trigger attacks within ~1 ms", "[lpg]")
{
    LowPassGate g;
    g.prepare(kSR);
    g.setBrightness(1.0f);
    g.setDamping(0.5f);
    g.trigger(1.0f);

    // Single-pole RC attack with τ = kAttackSec = 1 ms.  After 3 τ = 3 ms,
    // the RC value is at 1 − e⁻³ ≈ 0.95 of the target.  We feed a DC
    // unity carrier and read back the output — since cutoff at envelope→1
    // is the open frequency, DC is unfiltered (pass-band at the LP), so
    // the output equals (envelope × DC-gain × 1.0) = envelope (the LP DC
    // gain of an RBJ LP is exactly 1).  Sample at the 3 ms mark.
    const int nAttackEnd = static_cast<int>(kSR * 0.003f);
    float last = 0.0f;
    for (int n = 0; n <= nAttackEnd; ++n)
        last = g.process(1.0f);

    // Threshold 0.9: 1 − e⁻³ ≈ 0.9502.  Allow 5 % slack against single-
    // pole coefficient quantisation at 44.1 kHz (kAttackCoef precision
    // ≈ 1e-3 relative).  This catches an attack-coefficient bug while
    // staying analytically derivable.
    REQUIRE(last > 0.9f);
}

TEST_CASE("LowPassGate: damping=0 falls fast, damping=1 falls slow", "[lpg]")
{
    auto envAt = [&](float damping, float timeSec)
    {
        LowPassGate g;
        g.prepare(kSR);
        g.setBrightness(1.0f);
        g.setDamping(damping);
        g.trigger(1.0f);
        // Settle the attack (>3τ).
        for (int n = 0; n < static_cast<int>(kSR * 0.005f); ++n)
            (void)g.process(1.0f);
        // Sample the LP output at `timeSec` from now (carrier = DC unity).
        const int n = static_cast<int>(kSR * timeSec);
        float out = 0.0f;
        for (int i = 0; i < n; ++i) out = g.process(1.0f);
        return out;
    };

    // damping=0 → kMinFallSec (80 ms), e⁻¹ ≈ 37 % at 80 ms.
    // damping=1 → kMaxFallSec (600 ms), e⁻¹ ≈ 37 % at 600 ms.
    // After 200 ms:
    //   damping=0 → e^(−200/80) ≈ 0.082    (well below 0.5)
    //   damping=1 → e^(−200/600) ≈ 0.717   (well above 0.5)
    // Threshold 0.5 separates the two cleanly; physical derivation, not
    // implementation-fitted.
    const float envFast = envAt(0.0f, 0.2f);
    const float envSlow = envAt(1.0f, 0.2f);
    REQUIRE(envFast < 0.5f);
    REQUIRE(envSlow > 0.5f);
    REQUIRE(envSlow > envFast * 4.0f);  // analytical ratio ≈ 8.7×, margin 2× for filter LP state
}

// --- Brightness: peak cutoff controls VCF passband ----------------------

TEST_CASE("LowPassGate: brightness=0 attenuates carrier; brightness=1 passes", "[lpg]")
{
    // At envelope peak (≈1), cutoff = kClosedCutoff × (kOpenCutoff/kClosedCutoff)^brightness.
    //   brightness=0 → cutoff = 50 Hz.  440 Hz is 8.8× cutoff → 1-pole-equivalent
    //                  attenuation ≈ 1 / (1 + 8.8²) ≈ 0.013 (−37 dB).
    //   brightness=1 → cutoff = 6000 Hz.  440 Hz is 0.073× cutoff → passband.
    // Ratio (bright / dark) should be at least 10×; analytical ratio at
    // 440 Hz vs cutoff=50 Hz vs 6000 Hz is ≈ 75× — margin 7.5× for filter
    // (RBJ LP, Q=0.707 not 1-pole) and envelope-peak tracking.
    auto magnitudeAt = [&](float brightness)
    {
        LowPassGate g;
        g.prepare(kSR);
        g.setBrightness(brightness);
        g.setDamping(1.0f);  // long fall — envelope stays near peak for the test window
        g.trigger(1.0f);
        // Let attack complete.
        for (int n = 0; n < static_cast<int>(kSR * 0.005f); ++n)
            (void)g.process(sine440(n, kSR));
        // 250 ms window at 440 Hz — bin-aligned (= 110 cycles).
        const int N = static_cast<int>(kSR * 0.25f);
        std::vector<float> out(N);
        for (int n = 0; n < N; ++n)
            out[n] = g.process(sine440(n + static_cast<int>(kSR * 0.005f), kSR));
        return goertzelMagnitude(out, 440.0f, kSR);
    };

    const float magBright = magnitudeAt(1.0f);
    const float magDark   = magnitudeAt(0.0f);
    INFO("magBright=" << magBright << "  magDark=" << magDark);
    REQUIRE(magBright > magDark * 10.0f);
}

// --- Output range -------------------------------------------------------

TEST_CASE("LowPassGate: output bounded by carrier × envelope", "[lpg]")
{
    LowPassGate g;
    g.prepare(kSR);
    g.setBrightness(1.0f);
    g.setDamping(0.5f);
    g.trigger(1.0f);

    // Carrier ±1, envelope ∈ [0, 1], LP DC gain = 1 (RBJ LP with Q=0.707
    // peaks at most ~1 dB above unity, ≈ 1.12).  So output bound is
    // ±1.12 in steady state.  Margin 1e-3 for ULP / filter-state
    // transient (during the cutoff sweep there can be brief excursion
    // up to ~1.2 — observed-ceiling-derived, but we publish 1.3 for
    // safety per "Output levels" table convention).
    auto out = std::vector<float>(static_cast<int>(kSR * 0.5f));
    for (size_t n = 0; n < out.size(); ++n)
        out[n] = g.process(sine440(static_cast<int>(n), kSR));

    REQUIRE(peakAbs(out) <= 1.3f);
    // Sanity: not silent.  At full bright + decay attack, peak should
    // clear half-amplitude trivially.
    REQUIRE(peakAbs(out) > 0.1f);
}

// --- Re-trigger during Decay continues smoothly -------------------------

TEST_CASE("LowPassGate: re-trigger during Decay does not click", "[lpg]")
{
    LowPassGate g;
    g.prepare(kSR);
    g.setBrightness(1.0f);
    g.setDamping(1.0f);
    g.trigger(1.0f);

    // Let attack complete and decay run for ~50 ms.  The envelope at that
    // point is `e^(-0.05 / 0.6) ≈ 0.92` of the peak (damping=1 → 600 ms τ).
    for (int n = 0; n < static_cast<int>(kSR * 0.05f); ++n)
        (void)g.process(1.0f);

    // Re-trigger.  The implementation contract: stage_ flips to Attack
    // but envelope_ is preserved (no jump back to 0), so the next sample
    // is at most one attack-coefficient step away from the previous
    // sample.  attackCoef = 1 - e^(-1/(0.001 × 44100)) ≈ 0.0223 per sample.
    // The pre-/post-trigger sample-to-sample delta is bounded by
    //   |attackCoef × (triggerLevel - prev_env)|
    // ≤ attackCoef × (1.0 - 0.0)  = 0.0223  (assuming triggerLevel=1).
    // We allow 0.05 (≈ 2× headroom) for filter-cutoff transient effects
    // from the sudden cutoff sweep change.
    const float before = g.process(1.0f);
    g.trigger(1.0f);
    const float after  = g.process(1.0f);
    INFO("before=" << before << " after=" << after << " delta=" << (after - before));
    REQUIRE(std::fabs(after - before) < 0.05f);
}

// --- Velocity scales envelope peak --------------------------------------

TEST_CASE("LowPassGate: trigger velocity scales envelope linearly", "[lpg]")
{
    auto peakLevel = [&](float velocity)
    {
        LowPassGate g;
        g.prepare(kSR);
        g.setBrightness(1.0f);
        g.setDamping(1.0f);
        g.trigger(velocity);
        // Settle attack.
        float maxOut = 0.0f;
        for (int n = 0; n < static_cast<int>(kSR * 0.01f); ++n)
            maxOut = std::max(maxOut, std::fabs(g.process(1.0f)));
        return maxOut;
    };

    // Envelope peak ≈ velocity (single-pole RC target).  Ratio velocity
    // 1.0 / 0.5 = 2.0 exactly; allow 5 % drift for the LP DC-gain
    // contribution.
    const float p1   = peakLevel(1.0f);
    const float p05  = peakLevel(0.5f);
    REQUIRE_THAT(p1 / std::max(p05, 1e-6f), WithinAbs(2.0f, 0.1f));
}

// --- Parameter clamping -------------------------------------------------

TEST_CASE("LowPassGate: parameter clamping", "[lpg]")
{
    LowPassGate g;
    g.prepare(kSR);

    g.setDamping(-1.0f);
    g.setDamping(5.0f);
    g.setBrightness(-1.0f);
    g.setBrightness(10.0f);
    g.trigger(-1.0f);
    g.trigger(10.0f);

    for (int n = 0; n < 4410; ++n)
    {
        float out = g.process(sine440(n, kSR));
        REQUIRE(std::isfinite(out));
        REQUIRE(std::fabs(out) <= 1.3f);
    }
}

// --- Long-run stability with repeated triggers --------------------------

TEST_CASE("LowPassGate: 10s stability under repeated triggering", "[lpg][stability]")
{
    LowPassGate g;
    g.prepare(kSR);
    g.setBrightness(0.7f);
    g.setDamping(0.3f);

    const int N = 10 * static_cast<int>(kSR);
    // Trigger every 100 ms — 100 pings over 10 s — to exercise the
    // Stage::Attack ↔ Stage::Decay transitions and the silence flush
    // threshold under sustained operation.
    const int triggerEvery = static_cast<int>(kSR * 0.1f);
    for (int n = 0; n < N; ++n)
    {
        if (n % triggerEvery == 0) g.trigger(1.0f);
        float out = g.process(sine440(n, kSR));
        REQUIRE(std::isfinite(out));
        REQUIRE(std::fabs(out) <= 1.3f);
    }
}

// ============================================================
// LowPassGateVoice — carrier + LPG bundle
// ============================================================

#include <ideath/LowPassGateVoice.h>

TEST_CASE("LowPassGateVoice: silent until pinged", "[lpgvoice]")
{
    LowPassGateVoice v;
    v.prepare(kSR);
    v.setFrequency(220.0f);
    v.setTone(0.5f);
    v.setBrightness(1.0f);
    v.setDamping(0.5f);

    // No ping() → LPG envelope idle → output = 0 regardless of internal
    // carrier oscillator's running state.
    for (int n = 0; n < 1000; ++n)
        REQUIRE(v.process() == 0.0f);
}

TEST_CASE("LowPassGateVoice: ping produces sound at the carrier fundamental", "[lpgvoice]")
{
    LowPassGateVoice v;
    v.prepare(kSR);
    v.setFrequency(440.0f);
    v.setTone(1.0f);        // saw
    v.setBrightness(1.0f);  // wide-open LPG
    v.setDamping(1.0f);     // long fall — keeps envelope above 0.5 for the window
    v.ping(1.0f);

    // Skip attack transient (5 ms ≥ 3τ at 1 ms attack).
    for (int n = 0; n < static_cast<int>(kSR * 0.005f); ++n)
        (void)v.process();

    // 250 ms = 110 cycles of 440 Hz, bin-aligned.
    const int N = static_cast<int>(kSR * 0.25f);
    std::vector<float> out(N);
    for (int n = 0; n < N; ++n) out[n] = v.process();

    const float magOn  = goertzelMagnitude(out, 440.0f, kSR);
    const float magOff = goertzelMagnitude(out, 313.0f, kSR);  // non-harmonic

    // Carrier (saw) has rich harmonic content but the LPG with brightness=1
    // keeps the fundamental and a few low harmonics fully present.
    // Fundamental bin amplitude for a unit-amplitude saw is 2/π ≈ 0.637,
    // scaled by envelope-peak (~1) × LP-DC-gain (~1) ≈ 0.637.  Off-bin
    // 313 Hz is non-harmonic, well into the LP stop-band of the saw's
    // own spectrum decay (1/n).  Ratio 5× is conservative.
    REQUIRE(magOn > magOff * 5.0f);
    // Audibility floor: 0.01 = −40 dBFS, well below 0.637 × headroom but
    // above any numerical floor.
    REQUIRE(magOn > 0.01f);
}

TEST_CASE("LowPassGateVoice: tone morph changes spectrum", "[lpgvoice]")
{
    auto captureAt = [&](float tone)
    {
        LowPassGateVoice v;
        v.prepare(kSR);
        v.setFrequency(110.0f);
        v.setTone(tone);
        v.setBrightness(1.0f);
        v.setDamping(1.0f);
        v.ping(1.0f);
        for (int n = 0; n < static_cast<int>(kSR * 0.005f); ++n)
            (void)v.process();
        std::vector<float> out(static_cast<int>(kSR * 0.25f));
        for (size_t n = 0; n < out.size(); ++n) out[n] = v.process();
        return out;
    };

    auto outSaw    = captureAt(1.0f);  // pure saw
    auto outSquare = captureAt(0.0f);  // pure square

    // Saw fundamental amplitude = 2/π ≈ 0.637; saw has all integer
    // harmonics with amplitude 2/(π n).
    // Square fundamental amplitude = 4/π ≈ 1.273; square has odd
    // harmonics only.
    // At the 2nd harmonic (220 Hz, n=2):
    //   Saw → 2/(π × 2) = 0.318 (present)
    //   Square → 0 (no even harmonics)
    // After LPG (cutoff at 6000 Hz, both pass freely), the spectral
    // contrast is preserved.  Saw 2nd-harmonic energy ≫ Square 2nd.
    const float saw2nd    = goertzelMagnitude(outSaw,    220.0f, kSR);
    const float square2nd = goertzelMagnitude(outSquare, 220.0f, kSR);
    INFO("saw2nd=" << saw2nd << "  square2nd=" << square2nd);
    REQUIRE(saw2nd > square2nd * 5.0f);
}

TEST_CASE("LowPassGateVoice: output bounded", "[lpgvoice]")
{
    LowPassGateVoice v;
    v.prepare(kSR);
    v.setFrequency(220.0f);
    v.setTone(1.0f);
    v.setBrightness(1.0f);
    v.setDamping(0.5f);
    v.ping(1.0f);

    auto out = std::vector<float>(static_cast<int>(kSR));
    for (size_t n = 0; n < out.size(); ++n) out[n] = v.process();
    // Same bound as LowPassGate: ±1.3 (LP near-unity gain × ±1 carrier ×
    // envelope ≤ 1).
    REQUIRE(peakAbs(out) <= 1.3f);
}

TEST_CASE("LowPassGateVoice: parameter clamping", "[lpgvoice]")
{
    LowPassGateVoice v;
    v.prepare(kSR);
    // Push each setter past both ends.  None should crash or NaN.
    v.setFrequency(-100.0f);
    v.setFrequency(1e6f);
    v.setTone(-1.0f);
    v.setTone(5.0f);
    v.setBrightness(-1.0f);
    v.setBrightness(5.0f);
    v.setDamping(-1.0f);
    v.setDamping(5.0f);
    v.ping(-1.0f);
    v.ping(10.0f);

    for (int n = 0; n < 4410; ++n)
    {
        float out = v.process();
        REQUIRE(std::isfinite(out));
        REQUIRE(std::fabs(out) <= 1.3f);
    }
}
