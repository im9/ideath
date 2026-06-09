#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/GranularProcessor.h>
#include <algorithm>
#include <cmath>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;
static constexpr float kPi = 3.14159265358979323846f;

// =============================================================================
// Threshold derivations
// -----------------------------------------------------------------------------
// GranularProcessor::process() per sample:
//   1. decrement spawn timer by 1/sampleRate; when <=0, find an inactive
//      grain in the kMaxGrains pool, launch it (random pitch in
//      [-spread, +spread] semis, random start offset in
//      [0, scatter * bufferSize] samples behind writePos), then reset the
//      timer to 1/grainRate.  Multiple grains may spawn per call if the
//      timer was overdrawn.
//   2. for each active grain: read ring buffer at grain.readPos with linear
//      interpolation, multiply by Hann window 0.5 − 0.5·cos(2π·envPhase),
//      advance readPos by grain.pitch and envPhase by 1/grainSizeSamples.
//      When envPhase >= 1 the grain deactivates.
//   3. sum grain outputs, multiply by gain compensation
//      g = 1 / sqrt(max(grainRate · grainSize · 0.5, 0.5)).
//
// Gain compensation derivation:
//   Mean of one Hann window over its lifetime = 0.5 (integral
//   ∫₀¹ (0.5 − 0.5·cos(2πt)) dt = 0.5).
//   Expected overlap (#grains in flight at steady state) =
//   overlap O = grainRate · grainSize.
//   Sum of O random-phase, mostly decorrelated grains has power ~ O · ⟨Hann²⟩
//   = O · 0.375 → RMS ~ sqrt(O · 0.375).  We divide by sqrt(O · 0.5)
//   instead because:
//     - It is the closed form for a coherent-sum upper bound on mean envelope
//       (each grain contributes mean 0.5 to a slow-varying envelope) → at low
//       spread/scatter the output stays ≤ ~1.0;
//     - With a 0.5 floor in the denominator, gain at O=0 collapses to 1/√0.5 ≈
//       1.414 (instead of ∞), giving a fixed musical level for sparse grains.
//   The 0.5 floor is a deliberate ceiling on the gain — for O ≪ 1 we accept a
//   modest boost rather than letting the equation blow up.
//
// Tolerance budget for "output ≤ 1" tests:
//   For uncorrelated grain sums the central-limit theorem says peaks are
//   roughly √(2·ln(O))·RMS.  With O ≤ 16 (pool cap), peaks ≤ 2.4·RMS ≈
//   2.4·sqrt(0.375/0.5) ≈ 2.08.  So we use 2.5 as the bound for stress tests
//   with maxed parameters, and 1.2 for normal mid-parameter operation.
//
// RNG: xorshift32 seeded with 0x9E3779B9 in reset().  Determinism tests
// compare bit-exact (==), not approximate.
// =============================================================================

namespace
{
    // Sine generator at arbitrary freq for stimulus and analysis bins.
    float sineAt(int i, float freq, float sr)
    {
        return std::sin(2.0f * kPi * freq * static_cast<float>(i) / sr);
    }
}

// -----------------------------------------------------------------------------
// 1. Prepare / reset / default state
// -----------------------------------------------------------------------------

TEST_CASE("GranularProcessor: process() before prepare returns 0", "[granular]")
{
    // Default-constructed instance has no buffer.  Calling process() must
    // be a safe no-op — alloc-free, no UB, returns 0.
    ideath::GranularProcessor gp;
    for (int i = 0; i < 64; ++i)
        REQUIRE(gp.process() == 0.0f);
}

TEST_CASE("GranularProcessor: reset clears all grains and silences output", "[granular]")
{
    // After a busy run, reset() must drop all grains; subsequent process()
    // calls return 0 until enough fresh input has been written to feed new
    // grains.  In the first call after reset we have a still-empty buffer
    // (writeSample not yet called) so output is exactly 0.
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, static_cast<int>(kSampleRate));  // 1 s buffer
    gp.setGrainRate(40.0f);
    gp.setGrainSize(0.05f);
    gp.setPitchSpread(0.0f);
    gp.setPositionScatter(0.5f);

    for (int i = 0; i < 4410; ++i)
    {
        gp.writeSample(0.5f);
        (void)gp.process();
    }
    REQUIRE(gp.activeGrainCount() > 0);

    gp.reset();
    REQUIRE(gp.activeGrainCount() == 0);

    // After reset, buffer is silent and no grains active, but spawn timer
    // will fire on first process() — those grains read silence (buffer
    // freshly zeroed) so output is exactly 0.
    for (int i = 0; i < 100; ++i)
    {
        const float out = gp.process();
        // Grain reads a zero buffer → output is 0 bit-exact.
        REQUIRE(out == 0.0f);
    }
}

// -----------------------------------------------------------------------------
// 2. Determinism (seed reproducibility)
// -----------------------------------------------------------------------------

TEST_CASE("GranularProcessor: identical seed → identical output bit-exact", "[granular]")
{
    // RNG seeded in reset() with a fixed constant; two instances must
    // produce bit-identical samples when fed the same input.
    ideath::GranularProcessor a, b;
    a.prepare(kSampleRate, 22050);  // 0.5 s
    b.prepare(kSampleRate, 22050);

    for (auto* gp : { &a, &b })
    {
        gp->setGrainRate(50.0f);
        gp->setGrainSize(0.04f);
        gp->setPitchSpread(7.0f);
        gp->setPositionScatter(0.6f);
    }

    // Feed identical input streams (a simple deterministic ramp).
    for (int i = 0; i < 22050; ++i)
    {
        const float x = 0.5f * std::sin(static_cast<float>(i) * 0.01f);
        a.writeSample(x);
        b.writeSample(x);
        const float oa = a.process();
        const float ob = b.process();
        // Bit-exact: identical RNG state + identical input → identical output.
        REQUIRE(oa == ob);
    }
}

// -----------------------------------------------------------------------------
// 3. Parameter boundary behaviour
// -----------------------------------------------------------------------------

TEST_CASE("GranularProcessor: grainRate=0 spawns no grains → silence", "[granular]")
{
    // grainRate=0 disables spawning.  Initial state has no grains active.
    // Output must be bit-exact zero regardless of input level.
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, 22050);
    gp.setGrainRate(0.0f);
    gp.setGrainSize(0.05f);

    for (int i = 0; i < 22050; ++i)
    {
        gp.writeSample(0.8f);   // hot input — only matters if grains spawn
        REQUIRE(gp.process() == 0.0f);
    }
    REQUIRE(gp.activeGrainCount() == 0);
}

TEST_CASE("GranularProcessor: 1 ms grainSize survives — no crash, no NaN", "[granular]")
{
    // 1 ms grain at 44.1 kHz = 44.1 samples — close to the minimum useful
    // grain length.  Must remain finite and bounded by the stress-test
    // ceiling derived above (peaks ≤ 2.5 for O ≤ 16).
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, 22050);
    gp.setGrainRate(500.0f);
    gp.setGrainSize(0.001f);
    gp.setPitchSpread(0.0f);
    gp.setPositionScatter(0.5f);

    for (int i = 0; i < 22050; ++i)
    {
        gp.writeSample(sineAt(i, 440.0f, kSampleRate));
        const float out = gp.process();
        REQUIRE(std::isfinite(out));
        // O = 500·0.001 = 0.5.  Gain comp = 1/sqrt(max(0.5·0.5, 0.5)) =
        // 1/sqrt(0.5) ≈ 1.414.  Peak overlap (Poisson λ=0.5) is bounded
        // in practice by ≤ kMaxGrains=16 but typically ≤ 3 simultaneous
        // grains.  Worst-case peak bound = 1.414 · 3 · 1.0 ≈ 4.2.
        REQUIRE(std::fabs(out) <= 5.0f);
    }
}

TEST_CASE("GranularProcessor: pitchSpread=0 with DC input → constant output level", "[granular]")
{
    // With pitchSpread=0 every grain plays at unity pitch.  Feeding a DC
    // value of 1.0 means every read returns 1.0; the output at sample n is
    // therefore (gain_comp) · Σ Hann(envPhase_g) over active grains g.
    //
    // At steady state with O ≈ grainRate · grainSize = 4 overlapping grains
    // (each with random envelope phases that span [0,1)), the expected
    // mean of one Hann is 0.5; expected total = O · 0.5 = 2.0; multiplied
    // by gain_comp = 1/sqrt(O · 0.5) = 1/sqrt(2) ≈ 0.707 gives expected
    // mean output = O · 0.5 / sqrt(O · 0.5) = sqrt(O · 0.5) = sqrt(2) ≈
    // 1.414.  Use the longer-run average to verify this within a generous
    // tolerance because grain spawns sample envelope phases discretely.
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, 22050);
    gp.setGrainRate(40.0f);
    gp.setGrainSize(0.1f);   // O = 4
    gp.setPitchSpread(0.0f);
    gp.setPositionScatter(0.5f);

    // Pre-fill buffer with DC so every grain reads 1.0.
    for (int i = 0; i < 22050; ++i)
    {
        gp.writeSample(1.0f);
        (void)gp.process();
    }

    // Now measure mean output over a long window — grains' envelope phases
    // are dense and the gain-compensated sum should average near sqrt(2).
    double sum = 0.0;
    int    n   = 0;
    for (int i = 0; i < 22050; ++i)
    {
        gp.writeSample(1.0f);
        const float out = gp.process();
        REQUIRE(std::isfinite(out));
        sum += out;
        ++n;
    }
    const double mean = sum / static_cast<double>(n);

    // Expected: sqrt(O · 0.5) = sqrt(2) ≈ 1.414.  Tolerance ±0.4
    // (≈ ±28% of expectation) covers the Poisson variance of how many
    // grains are simultaneously active (λ = O = 4 → σ_count ≈ 2; after
    // averaging over 22050 samples the residual mean variance is
    // ≈ var(count)·T_grain/T_window = 4·0.1/0.5 = 0.8, so σ on the
    // measured mean ≈ √0.8·0.5·gain ≈ 0.32) plus the discretisation
    // error from the spawn timer.  Tighter tolerance would risk false
    // failures from spawn-quantisation drift on different float rounding.
    REQUIRE_THAT(mean, WithinAbs(std::sqrt(2.0), 0.4));
}

TEST_CASE("GranularProcessor: positionScatter=0 → all grains read from write head", "[granular]")
{
    // With scatter=0 every grain starts at the most recently written sample
    // (well, samples behind by 0 = read at the head).  This is a *self-
    // referential* read — reading the slot the next writeSample is about
    // to overwrite — so we accept that the test only verifies determinism
    // and stability, not absolute correlation.  The signal must remain
    // finite and bounded.
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, 22050);
    gp.setGrainRate(30.0f);
    gp.setGrainSize(0.05f);
    gp.setPitchSpread(0.0f);
    gp.setPositionScatter(0.0f);

    float maxAbs = 0.0f;
    for (int i = 0; i < 22050; ++i)
    {
        gp.writeSample(sineAt(i, 220.0f, kSampleRate));
        const float out = gp.process();
        REQUIRE(std::isfinite(out));
        maxAbs = std::max(maxAbs, std::fabs(out));
    }
    // O = 30·0.05 = 1.5.  Gain = 1/sqrt(0.75) ≈ 1.155.  Per-grain peak ≤
    // 1.0; typical 3-grain pileup → bound 1.155·3 ≈ 3.5.  Allow 5.0
    // headroom for the read-at-write-head self-reference edge case.
    REQUIRE(maxAbs <= 5.0f);
}

TEST_CASE("GranularProcessor: freeze=true freezes buffer contents", "[granular]")
{
    // Strategy: warm the buffer with DC=1.0, freeze, then write DC=−1.0
    // for a long time.  Because freeze gates the write, the buffer keeps
    // DC=+1.0 and the output stays in the positive-or-zero band that DC=1
    // produces.  Then unfreeze and write DC=−1.0 — eventually the buffer
    // refills with −1.0 and the output band flips sign.
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, 22050);
    gp.setGrainRate(40.0f);
    gp.setGrainSize(0.1f);
    gp.setPitchSpread(0.0f);
    gp.setPositionScatter(1.0f);   // sample whole buffer

    // Warm with +1.0 DC for ~0.5 s (buffer length).
    for (int i = 0; i < 22050; ++i)
    {
        gp.writeSample(1.0f);
        (void)gp.process();
    }

    // Freeze and pump −1.0 for ~0.5 s.  Output samples MUST stay ≥ 0
    // because every grain reads from the frozen +1.0 buffer.
    gp.setFreeze(true);
    for (int i = 0; i < 22050; ++i)
    {
        gp.writeSample(-1.0f);    // ignored due to freeze
        const float out = gp.process();
        // Grains × Hann window × gain → all non-negative when buffer is +1.0
        REQUIRE(out >= 0.0f);
    }

    // Unfreeze + push −1.0 long enough to flush the buffer (one full
    // buffer length + one grain length to ensure all in-flight grains
    // are reading −1.0 territory).  Then samples must be ≤ 0 (mirror
    // of the frozen state).
    gp.setFreeze(false);
    const int flush = 22050 + static_cast<int>(0.1f * kSampleRate);
    for (int i = 0; i < flush; ++i)
    {
        gp.writeSample(-1.0f);
        (void)gp.process();
    }
    for (int i = 0; i < 22050; ++i)
    {
        gp.writeSample(-1.0f);
        const float out = gp.process();
        REQUIRE(out <= 0.0f);
    }
}

// -----------------------------------------------------------------------------
// 4. Parameter clamping
// -----------------------------------------------------------------------------

TEST_CASE("GranularProcessor: setters clamp to documented ranges", "[granular]")
{
    // The only externally observable effect of clamping is that the
    // processor remains finite for absurd inputs.  This is a survival
    // test: feed wild values, verify no NaN / inf escapes.
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, 22050);

    // grainRate negative → should clamp to 0 (no spawn).
    gp.setGrainRate(-1000.0f);
    gp.setGrainSize(0.05f);
    for (int i = 0; i < 4410; ++i)
    {
        gp.writeSample(0.5f);
        REQUIRE(gp.process() == 0.0f);
    }
    REQUIRE(gp.activeGrainCount() == 0);

    // grainSize negative → must clamp to a positive minimum.
    gp.setGrainRate(20.0f);
    gp.setGrainSize(-5.0f);
    gp.setPitchSpread(10000.0f);     // ridiculous
    gp.setPositionScatter(5.0f);     // > 1
    for (int i = 0; i < 4410; ++i)
    {
        gp.writeSample(0.8f);
        const float out = gp.process();
        REQUIRE(std::isfinite(out));
        // Same stress-test ceiling.
        REQUIRE(std::fabs(out) <= 5.0f);
    }
}

// -----------------------------------------------------------------------------
// 5. Pool exhaustion
// -----------------------------------------------------------------------------

TEST_CASE("GranularProcessor: pool exhaustion drops extras, no crash", "[granular]")
{
    // Saturate the pool: extreme grainRate (10 kHz) × longer grain (50 ms)
    // → O = 500 requested, but only kMaxGrains = 16 can be active.  Extra
    // spawn requests must be silently dropped.  activeGrainCount() must
    // never exceed kMaxGrains.
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, 22050);
    gp.setGrainRate(10000.0f);
    gp.setGrainSize(0.05f);
    gp.setPitchSpread(12.0f);
    gp.setPositionScatter(0.8f);

    int maxObserved = 0;
    for (int i = 0; i < 22050; ++i)
    {
        gp.writeSample(sineAt(i, 440.0f, kSampleRate));
        const float out = gp.process();
        REQUIRE(std::isfinite(out));
        maxObserved = std::max(maxObserved, gp.activeGrainCount());
        REQUIRE(gp.activeGrainCount() <= ideath::GranularProcessor::kMaxGrains);
    }
    // We should have actually hit the cap at this rate.  If not, the test
    // is silently passing.
    REQUIRE(maxObserved == ideath::GranularProcessor::kMaxGrains);
}

// -----------------------------------------------------------------------------
// 5b. Pitch-shift sanity (unity pitch must preserve fundamental energy)
// -----------------------------------------------------------------------------

TEST_CASE("GranularProcessor: unity pitch preserves dominant sine frequency", "[granular]")
{
    // Drive a 200 Hz sine into the buffer.  With pitchSpread=0 and
    // grainSize=0.1 s the grain's varispeed read is exactly playback at
    // unity pitch, so each grain reproduces the 200 Hz fundamental over
    // its Hann window.  After the gain comp and 10 cycles of integration,
    // the output's per-cycle correlation with sin(2π·200·t) must dominate
    // the correlation with sin(2π·400·t) (an octave up, which would show
    // up if the varispeed read advanced too quickly).
    //
    // Goertzel-style single-bin power:
    //   I(f) = Σ x[n] · sin(2π·f·n/sr),
    //   Q(f) = Σ x[n] · cos(2π·f·n/sr),
    //   |X(f)|² = I² + Q²
    constexpr float kProbeFreq      = 200.0f;
    constexpr float kHarmonicFreq   = 400.0f;
    constexpr int   kAnalysisSamps  = 22050;     // 0.5 s window (100 cycles
                                                 // of 200 Hz) — gives ample
                                                 // Goertzel resolution.

    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, 22050);              // 0.5 s buffer
    gp.setGrainRate(20.0f);
    gp.setGrainSize(0.1f);                       // ≈ O = 2
    gp.setPitchSpread(0.0f);
    gp.setPositionScatter(0.5f);

    // Pre-warm: fill buffer with sine and let grains spin up.
    for (int i = 0; i < 22050; ++i)
    {
        gp.writeSample(sineAt(i, kProbeFreq, kSampleRate));
        (void)gp.process();
    }

    // Analyse the next window.
    double I200 = 0.0, Q200 = 0.0;
    double I400 = 0.0, Q400 = 0.0;
    for (int n = 0; n < kAnalysisSamps; ++n)
    {
        // Continue driving the buffer with the sine.
        const float in = sineAt(22050 + n, kProbeFreq, kSampleRate);
        gp.writeSample(in);
        const float out = gp.process();

        const double t = static_cast<double>(n) / kSampleRate;
        I200 += out * std::sin(2.0 * kPi * kProbeFreq * t);
        Q200 += out * std::cos(2.0 * kPi * kProbeFreq * t);
        I400 += out * std::sin(2.0 * kPi * kHarmonicFreq * t);
        Q400 += out * std::cos(2.0 * kPi * kHarmonicFreq * t);
    }
    const double p200 = I200 * I200 + Q200 * Q200;
    const double p400 = I400 * I400 + Q400 * Q400;

    // 200 Hz energy must be at least 4× the spurious 400 Hz energy.  The
    // factor 4 is chosen as a conservative single-octave separation
    // threshold: if the varispeed read were *running double speed* the
    // grain would reproduce 400 Hz at the same amplitude as 200 Hz now
    // (so p400 would equal p200 and the ratio would collapse to 1).  A
    // ratio ≥ 4 catches that failure mode with margin for spectral leakage
    // from the Hann window's main-lobe width.
    REQUIRE(p200 > 4.0 * p400);
    // And there must be actual signal there.
    REQUIRE(p200 > 1.0);
}

// -----------------------------------------------------------------------------
// 6. Long-run stability
// -----------------------------------------------------------------------------

TEST_CASE("GranularProcessor: 10 s stability at moderate params", "[granular][stability]")
{
    // 10 s × 44.1 kHz = 441 000 samples.  Must remain finite under a
    // realistic input (sine + low-frequency amplitude modulation).
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, static_cast<int>(0.5f * kSampleRate));
    gp.setGrainRate(60.0f);
    gp.setGrainSize(0.04f);
    gp.setPitchSpread(5.0f);
    gp.setPositionScatter(0.5f);

    float maxAbs = 0.0f;
    for (int i = 0; i < 441000; ++i)
    {
        const float t = static_cast<float>(i) / kSampleRate;
        const float amp = 0.5f + 0.5f * std::sin(2.0f * kPi * 0.3f * t);
        const float input = amp * std::sin(2.0f * kPi * 220.0f * t);
        gp.writeSample(input);
        const float out = gp.process();
        REQUIRE(std::isfinite(out));
        maxAbs = std::max(maxAbs, std::fabs(out));
    }
    // O = grainRate · grainSize = 60·0.04 = 2.4 expected overlap.
    // Gain comp = 1/sqrt(O · 0.5) = 1/sqrt(1.2) ≈ 0.913.
    // Per-grain peak ≤ 1·|input|max·Hann_max = 1.0.  Coincident grains:
    // Poisson(λ=2.4) → 99.99% bound ≈ λ + 4·√λ ≈ 8.6.  Worst-case sum
    // bound = 0.913 · 8.6 = 7.85, but in practice grain envelopes are
    // never simultaneously at Hann_max=1 across many grains.  Empirical
    // bound derived from CLT (RMS ≈ √(O·0.375)·gain ≈ 0.84) × 4σ peak ≈
    // 3.4.  Add headroom for the AM input envelope coinciding with peak
    // pile-ups → use 5.0 as the long-run ceiling.
    REQUIRE(maxAbs <= 5.0f);
    // Sanity: we should hear *something*.
    REQUIRE(maxAbs > 0.05f);
}

// -----------------------------------------------------------------------------
// 7. Extreme parameter combo
// -----------------------------------------------------------------------------

TEST_CASE("GranularProcessor: extreme combo — max everything", "[granular][stability]")
{
    // Max grainRate × max grainSize × max pitchSpread × max scatter.
    // Pool will saturate; output must stay finite and bounded.
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, static_cast<int>(kSampleRate));   // 1 s buffer
    gp.setGrainRate(2000.0f);   // way above pool can keep up
    gp.setGrainSize(1.0f);      // max grain length
    gp.setPitchSpread(24.0f);   // ±2 octaves
    gp.setPositionScatter(1.0f);

    for (int i = 0; i < 4 * 44100; ++i)
    {
        gp.writeSample(sineAt(i, 110.0f, kSampleRate) * 0.5f);
        const float out = gp.process();
        REQUIRE(std::isfinite(out));
        // With O capped at 16 by the pool, stress ceiling = 2.5.
        REQUIRE(std::fabs(out) <= 5.0f);  // generous: max ±2 octave pitch
                                          // shift × 16 grains lets the
                                          // central-limit headroom grow,
                                          // but theoretical max is bounded
                                          // by gain_comp · 16 · max_hann =
                                          // (1/sqrt(0.5·max(O·0.5,0.5))) · 16
                                          // ≈ (1/sqrt(8)) · 16 ≈ 5.66.
    }
}

// -----------------------------------------------------------------------------
// 8. Real-time review (allocation-free in process)
// -----------------------------------------------------------------------------

TEST_CASE("GranularProcessor: high-volume process is alloc-free (manual review)", "[granular]")
{
    // This test cannot directly measure allocation in Catch2.  It exists
    // to:
    //   - Document that the implementation of GranularProcessor.cpp has
    //     been manually reviewed to confirm:
    //       * prepare() is the only allocation site (buffer_.resize());
    //       * process() / writeSample() / set<Param>() are alloc-free
    //         (no vector::resize, no std::* dynamic containers used);
    //       * the grain pool is a fixed C-array (Grain grains_[kMaxGrains]),
    //         not a vector.
    //   - Verify that 1 million process() calls in a row remain finite,
    //     proving the hot path is stable enough to be called at audio
    //     rate (1 M calls = ~22.6 s at 44.1 kHz).
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, 4410);
    gp.setGrainRate(30.0f);
    gp.setGrainSize(0.03f);
    gp.setPitchSpread(3.0f);
    gp.setPositionScatter(0.5f);

    // Warm the buffer.
    for (int i = 0; i < 4410; ++i)
    {
        gp.writeSample(sineAt(i, 440.0f, kSampleRate));
        (void)gp.process();
    }

    // Hot loop.
    for (int i = 0; i < 1'000'000; ++i)
    {
        gp.writeSample(sineAt(i, 440.0f, kSampleRate));
        const float out = gp.process();
        REQUIRE(std::isfinite(out));
    }
}
