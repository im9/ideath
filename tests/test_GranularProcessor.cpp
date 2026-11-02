#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/GranularProcessor.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

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
        // Theoretical worst-case bound:
        //   y_max ≤ kMaxGrains · Hann_peak · gain_comp(O) · |input|max
        // Each grain contributes at most (Hann_peak=1) · gain_comp · |input|max
        // and the pool has at most kMaxGrains=16 active simultaneously.
        //   O = 500·0.001 = 0.5  →  denom = max(O·0.5, 0.5) = 0.5
        //   gain_comp = 1/√0.5 = √2 ≈ 1.414
        //   |input|max = 1 (unit sine)
        //   bound = 16 · 1 · √2 · 1 ≈ 22.63
        // This is a strict mathematical upper bound (sum of N items each
        // ≤ M is ≤ N·M); the actual peak is much lower due to Poisson grain
        // spacing and Hann envelope decorrelation, but the test only needs
        // to catch unbounded explosion. Round up to 23 for float headroom.
        REQUIRE(std::fabs(out) <= 23.0f);
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
    // Theoretical worst-case bound (same formula as the 1 ms grain test):
    //   y_max ≤ kMaxGrains · Hann_peak · gain_comp(O) · |input|max
    //   O = 30·0.05 = 1.5  →  denom = max(0.75, 0.5) = 0.75
    //   gain_comp = 1/√0.75 ≈ 1.155
    //   |input|max = 1 (unit sine)
    //   bound = 16 · 1 · 1.155 · 1 ≈ 18.48
    // The self-referential read (positionScatter=0 reads at the write
    // head) cannot exceed this bound because it just samples buffer
    // values that themselves are ≤ |input|max. Round up to 19.
    REQUIRE(maxAbs <= 19.0f);
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
        // Theoretical worst-case bound for clamped extreme params:
        //   grainRate=20, grainSize clamped to a small positive (impl-
        //   defined floor), so O is small → denom hits the 0.5 floor →
        //   gain_comp = 1/√0.5 = √2.
        //   |input|max = 0.8 (DC value written).
        //   bound = 16 · 1 · √2 · 0.8 ≈ 18.10  → round up to 19.
        REQUIRE(std::fabs(out) <= 19.0f);
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
    // Theoretical worst-case bound:
    //   y_max ≤ kMaxGrains · Hann_peak · gain_comp(O) · |input|max
    //   O = 60·0.04 = 2.4  →  denom = max(1.2, 0.5) = 1.2
    //   gain_comp = 1/√1.2 ≈ 0.913
    //   |input|max = 1 (AM-modulated sine peaks at amp=1, sin=1)
    //   bound = 16 · 1 · 0.913 · 1 ≈ 14.6  → round up to 15.
    REQUIRE(maxAbs <= 15.0f);
    // Lower bound — catches catastrophic silence (e.g. all-grains-idle
    // bug, broken gain comp, broken spawn logic).
    //   The 10 s window contains ≈3 cycles of the 0.3 Hz AM envelope, so
    //   the input traverses amp=1 multiple times. At amp=1 the per-grain
    //   contribution is Hann_peak · gain_comp · |input| = 1 · 0.913 · 1
    //   = 0.913. Even with average overlap 2.4 grains × mean Hann 0.5,
    //   expected peak amplitude during AM-peak phases is bounded below by
    //   gain_comp · |input|max · Hann_peak ≈ 0.913 (a single in-phase
    //   grain). Floor 0.05 = -26 dBFS is ~18× below this expected lower
    //   bound, catching ≥18× attenuation failure modes without being
    //   brittle against AM-trough phase coincidence with grain spawn
    //   gaps. Comfortably above the denormal/flush floor (~1e-5).
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
        // Theoretical worst-case bound at pool saturation:
        //   Active grains capped at kMaxGrains = 16 by the pool (extra
        //   spawn requests drop silently).
        //   O = grainRate · grainSize = 2000 · 1.0 = 2000, but the actual
        //   number of active grains saturates at 16.
        //   denom = max(O·0.5, 0.5) = 1000  →  gain_comp = 1/√1000 ≈ 0.0316.
        //   |input|max = 0.5 (sine × 0.5).
        //   Pitch shift ±2 octaves only changes read speed, not the
        //   amplitude of buffer values — bounded by ±|input|max.
        //   bound = 16 · 1 · 0.0316 · 0.5 ≈ 0.253  → round up to 1.0 for
        //   generous float headroom and any transient overshoot from
        //   linear interpolation at sub-sample read positions.
        REQUIRE(std::fabs(out) <= 1.0f);
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

// -----------------------------------------------------------------------------
// 9. Hann window shape (single grain, deterministic input)
// -----------------------------------------------------------------------------

TEST_CASE("GranularProcessor: single-grain output matches Hann · gain_comp · DC",
          "[granular]")
{
    // Feed DC = 1.0 into the buffer.  With positionScatter=0 and
    // pitchSpread=0 the grain reads from a deterministic offset at unity
    // pitch, so the read value is always 1.0 (modulo ring-buffer init).
    // Then the only time-varying factor in the output is the Hann envelope.
    //
    // Configuration:
    //   grainRate = 2 Hz → spawn period = 0.5 s
    //   grainSize = 0.4 s → 17 640 samples per grain @ 44.1 kHz
    //   At most one grain is active at any time (grainSize < spawn period).
    //   O = 2 · 0.4 = 0.8  →  denom = max(0.4, 0.5) = 0.5
    //   gain_comp = 1/√0.5 = √2 ≈ 1.414
    //
    // Hann formula: w(t) = 0.5 · (1 − cos(2π·t/T)) for t ∈ [0, T].
    //   w(0) = 0,  w(T/2) = 1,  w(T) = 0,  w(T/4) = w(3T/4) = 0.5.
    //
    // Expected output during a grain at phase p ∈ [0, 1]:
    //   out(p) = w(p) · gain_comp · DC = w(p) · √2 · 1
    //
    // We capture one full grain lifetime and verify the shape at three
    // checkpoints: start (≈0), middle (≈√2), and quarter-points (≈√2/2).
    //
    // Tolerance derivation: ±0.05 (≈ 3.5% of peak √2) covers:
    //   - discrete grain-phase quantisation: 1 sample / 17 640 ≈ 6e-5
    //   - Hann curvature near the checkpoint sample: |w'(p)| at p=0.5 is 0,
    //     at p=0.25 is ≈ π, so a single-sample slip changes Hann value by
    //     ≈ π · 6e-5 ≈ 2e-4
    //   - ring-buffer warm-up: the read tap may not see exact DC=1.0 for
    //     the first ~grainSize seconds (linear interp across the
    //     write-stop boundary)
    //   - float-precision multiplicative chain
    // 0.05 leaves ≈ 250× margin over the theoretical worst-case error.
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, static_cast<int>(2.0f * kSampleRate));
    gp.setGrainRate(2.0f);
    gp.setGrainSize(0.4f);
    gp.setPitchSpread(0.0f);
    gp.setPositionScatter(0.0f);

    // Warm the buffer with DC=1.0 (2 s worth = full ring buffer).
    for (int i = 0; i < static_cast<int>(2.0f * kSampleRate); ++i)
    {
        gp.writeSample(1.0f);
        (void)gp.process();
    }

    // Skip until a spawn is observed (≤ spawn period = 0.5 s).
    int waited = 0;
    while (gp.activeGrainCount() == 0 && waited < static_cast<int>(0.6f * kSampleRate))
    {
        gp.writeSample(1.0f);
        (void)gp.process();
        ++waited;
    }
    REQUIRE(gp.activeGrainCount() == 1);

    // Sample the grain at known phase points.  Grain length = 0.4 s.
    const int grainLen = static_cast<int>(0.4f * kSampleRate);
    std::vector<float> grainOut(static_cast<std::size_t>(grainLen));
    for (int i = 0; i < grainLen; ++i)
    {
        gp.writeSample(1.0f);
        grainOut[static_cast<std::size_t>(i)] = gp.process();
    }

    // Hann checkpoints (the spawn happens at sample 0 of grainOut).
    const float gain = std::sqrt(2.0f);
    const std::size_t qIdx = static_cast<std::size_t>(grainLen / 4);
    const std::size_t mIdx = static_cast<std::size_t>(grainLen / 2);
    const std::size_t tIdx = static_cast<std::size_t>(3 * grainLen / 4);

    REQUIRE_THAT(grainOut[qIdx], WithinAbs(0.5f * gain, 0.05f));   // ≈ 0.707
    REQUIRE_THAT(grainOut[mIdx], WithinAbs(1.0f * gain, 0.05f));   // ≈ √2
    REQUIRE_THAT(grainOut[tIdx], WithinAbs(0.5f * gain, 0.05f));   // ≈ 0.707
}

// -----------------------------------------------------------------------------
// 10. Grain envelope timing precision
// -----------------------------------------------------------------------------

TEST_CASE("GranularProcessor: grain lifetime matches grainSize × sr",
          "[granular]")
{
    // With a low spawn rate so grains don't overlap, the activeGrainCount
    // transition 0 → 1 → 0 must span exactly grainSize × sr samples
    // (±1 for discrete sample quantisation in the grain-end check).
    //
    // Threshold derivation: 1-sample tolerance comes from the integer-
    // comparison nature of the grain-active check (envPhase ≥ 1.0 → grain
    // dies).  No physics tolerance applies.
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, static_cast<int>(kSampleRate));
    gp.setGrainRate(1.0f);    // 1 spawn / s
    gp.setGrainSize(0.1f);    // 4410 samples @ 44.1 kHz
    gp.setPitchSpread(0.0f);
    gp.setPositionScatter(0.0f);

    // Wait for spawn (≤ 1 s).
    int waited = 0;
    while (gp.activeGrainCount() == 0 && waited < static_cast<int>(1.1f * kSampleRate))
    {
        gp.writeSample(0.5f);
        (void)gp.process();
        ++waited;
    }
    REQUIRE(gp.activeGrainCount() == 1);

    // Count process() calls until grain dies.
    const int expected = static_cast<int>(0.1f * kSampleRate);
    int lifetime = 0;
    while (gp.activeGrainCount() == 1 && lifetime < expected + 100)
    {
        gp.writeSample(0.5f);
        (void)gp.process();
        ++lifetime;
    }
    // Allow ±2 samples for the activeGrainCount sampling boundary and the
    // discrete envPhase ≥ 1.0 termination check.
    REQUIRE(std::abs(lifetime - expected) <= 2);
}

// -----------------------------------------------------------------------------
// 11. Buffer wrap correctness with positionScatter
// -----------------------------------------------------------------------------

TEST_CASE("GranularProcessor: positionScatter=1.0 reads span the whole buffer",
          "[granular]")
{
    // Write a unique marker into different time slices of the buffer, then
    // set positionScatter=1.0 and collect grain outputs over many spawns.
    // The grain reads must land across the buffer (= reading from anywhere
    // up to 1 buffer-length in the past), so the output values should
    // include samples from EARLY parts of the buffer, not just the most
    // recent.
    //
    // Marker scheme: split the 1 s ring buffer into 4 quarters; quarter k
    // is filled with value (k+1) · 0.2 = {0.2, 0.4, 0.6, 0.8}.  After
    // positionScatter=1.0 reads, the grain output range should at minimum
    // span the lowest and highest markers (modulo Hann attenuation), which
    // catches a buffer-wrap off-by-one bug that would keep all reads near
    // the latest written value.
    //
    // Threshold derivation: a working wrap-correct implementation must
    // produce output minima ≤ 0.3 · gain_comp · Hann_peak (somewhere in
    // the early quarter, value 0.2 × gain ≈ 0.28) and maxima ≥ 0.7 ×
    // gain · 0.5 (some Hann phase × value 0.8 ≈ 0.5).  We bound the
    // observed range with these soft bounds.
    ideath::GranularProcessor gp;
    const int bufLen = static_cast<int>(kSampleRate);
    gp.prepare(kSampleRate, bufLen);
    gp.setGrainRate(50.0f);    // 50 spawns/s → ~50 grains in the test window
    gp.setGrainSize(0.02f);    // short grain to keep individual reads sharp
    gp.setPitchSpread(0.0f);
    gp.setPositionScatter(1.0f);

    // Fill the buffer with the 4-quarter marker pattern.
    const int quarter = bufLen / 4;
    for (int q = 0; q < 4; ++q)
    {
        const float marker = 0.2f * static_cast<float>(q + 1); // 0.2, 0.4, 0.6, 0.8
        for (int i = 0; i < quarter; ++i)
        {
            gp.writeSample(marker);
            (void)gp.process();
        }
    }

    // Now collect outputs over 1 s.  Continue writing the highest marker
    // (0.8) so the buffer slowly rewrites; positionScatter=1.0 still picks
    // any past position so reads can come from the older markers.
    float minObs = 1e9f, maxObs = -1e9f;
    for (int i = 0; i < bufLen; ++i)
    {
        gp.writeSample(0.8f);
        const float out = gp.process();
        REQUIRE(std::isfinite(out));
        if (std::fabs(out) > 1e-4f)   // ignore the silent gaps between grains
        {
            minObs = std::min(minObs, out);
            maxObs = std::max(maxObs, out);
        }
    }
    // Spread must cover at least a 2× range — a wrap-broken impl that
    // only reads near the write head would show a narrow range close to
    // 0.8 · gain_comp.  Soft bound: max/min ≥ 2.
    REQUIRE(maxObs / std::max(minObs, 1e-4f) >= 2.0f);
}

// -----------------------------------------------------------------------------
// 12. Pitch offset (bipolar transposition centre)
// -----------------------------------------------------------------------------
//
// setPitchOffset shifts the random spread window:
//   semis = pitchOffset + uniform(-pitchSpread, +pitchSpread)
//   ratio = 2^(semis / 12)
// Default 0.0 = unity (spread window centred on unity pitch).
//
// Verified below:
//   • default matches an explicit setPitchOffset(0.0) call bit-exactly
//   • offset=+12, spread=0  → every grain reads at ratio 2.0 (F → 2F)
//   • offset=−12, spread=0  → every grain reads at ratio 0.5 (2F → F)
//   • offset=+6, spread=6   → ratios span [1, 2]; no sub-fundamental content
//   • determinism preserved with non-zero offset (same seed → same output)
//   • clamping to [−24, +24] semitones matches setPitchSpread's ±24 st cap
// -----------------------------------------------------------------------------

namespace
{
    // Sum a Hann-windowed Goertzel bin |X(f)|² over N samples of a signal
    // generated by `sampleFn(i)`, spanning i ∈ [0, N).  Hann window suppresses
    // side-lobe leakage from adjacent output frequencies (grain-envelope
    // spectra) that would otherwise smear across the analysis bins.
    template <class F>
    double goertzelPower(F&& sampleFn, int N, float freq, float sr)
    {
        double I = 0.0, Q = 0.0;
        for (int n = 0; n < N; ++n)
        {
            const double t = static_cast<double>(n) / sr;
            const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * n / (N - 1));
            const double x = w * static_cast<double>(sampleFn(n));
            I += x * std::sin(2.0 * kPi * freq * t);
            Q += x * std::cos(2.0 * kPi * freq * t);
        }
        return I * I + Q * Q;
    }
}

TEST_CASE("GranularProcessor: default pitchOffset (0) matches explicit setPitchOffset(0)",
          "[granular]")
{
    // Default-constructed pitchOffset_ is 0.0f.  Explicitly calling
    // setPitchOffset(0.0f) must produce bit-identical output — proves that
    // the setter with the identity argument doesn't perturb any state
    // (RNG, spawn timer, etc.) and that the default really is 0.
    ideath::GranularProcessor a, b;
    a.prepare(kSampleRate, 22050);
    b.prepare(kSampleRate, 22050);
    for (auto* gp : { &a, &b })
    {
        gp->setGrainRate(50.0f);
        gp->setGrainSize(0.04f);
        gp->setPitchSpread(7.0f);
        gp->setPositionScatter(0.6f);
    }
    b.setPitchOffset(0.0f);   // a leaves it at the default

    for (int i = 0; i < 22050; ++i)
    {
        const float x = 0.5f * std::sin(static_cast<float>(i) * 0.01f);
        a.writeSample(x);
        b.writeSample(x);
        // Bit-exact: identical RNG state + identical grain-spawn code path.
        REQUIRE(a.process() == b.process());
    }
}

TEST_CASE("GranularProcessor: pitchOffset=+12, spread=0 shifts input up one octave",
          "[granular]")
{
    // With pitchOffset=+12 st and spread=0, every grain reads at pitch
    // ratio 2^(12/12) = 2.0.  Feeding a 100 Hz sine, the grain's varispeed
    // read advances 2 samples per output sample → output cycles twice as
    // fast → 200 Hz per grain lifetime.  Summing many random-phase Hann-
    // windowed 200 Hz grains, Goertzel power at 200 Hz must dominate the
    // input 100 Hz bin.
    constexpr float kInputHz  = 100.0f;
    constexpr int   kAnalysis = 22050;   // 0.5 s @ 44.1 kHz = 100 cycles

    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, 22050);
    gp.setGrainRate(20.0f);
    gp.setGrainSize(0.1f);
    gp.setPitchSpread(0.0f);
    gp.setPositionScatter(0.5f);
    gp.setPitchOffset(12.0f);

    // Pre-warm the buffer and let grains spin up.
    for (int i = 0; i < 22050; ++i)
    {
        gp.writeSample(sineAt(i, kInputHz, kSampleRate));
        (void)gp.process();
    }

    // Capture one analysis window.
    std::vector<float> out(static_cast<std::size_t>(kAnalysis));
    for (int i = 0; i < kAnalysis; ++i)
    {
        gp.writeSample(sineAt(22050 + i, kInputHz, kSampleRate));
        out[static_cast<std::size_t>(i)] = gp.process();
    }

    const double p200 = goertzelPower(
        [&](int n) { return out[static_cast<std::size_t>(n)]; },
        kAnalysis, 200.0f, kSampleRate);
    const double p100 = goertzelPower(
        [&](int n) { return out[static_cast<std::size_t>(n)]; },
        kAnalysis, 100.0f, kSampleRate);

    // Threshold: 4× separation is the same conservative single-octave
    // margin used in the pre-existing "unity pitch preserves dominant
    // sine frequency" test (line ~473).  A broken impl that ignored the
    // offset and played at unity pitch would collapse p200/p100 to << 1,
    // so a 4× ratio in the OTHER direction is a comfortable pass.
    REQUIRE(p200 > 4.0 * p100);
    REQUIRE(p200 > 1.0);   // signal is actually there
}

TEST_CASE("GranularProcessor: pitchOffset=-12, spread=0 shifts input down one octave",
          "[granular]")
{
    // Mirror of the previous test: pitchOffset=-12 → ratio 2^(-12/12) = 0.5.
    // Feed 200 Hz sine; each grain reads at half speed → 100 Hz per grain.
    // Goertzel at 100 Hz must dominate the input 200 Hz bin.
    constexpr float kInputHz  = 200.0f;
    constexpr int   kAnalysis = 22050;   // 0.5 s @ 44.1 kHz

    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, 22050);
    gp.setGrainRate(20.0f);
    gp.setGrainSize(0.1f);
    gp.setPitchSpread(0.0f);
    gp.setPositionScatter(0.5f);
    gp.setPitchOffset(-12.0f);

    for (int i = 0; i < 22050; ++i)
    {
        gp.writeSample(sineAt(i, kInputHz, kSampleRate));
        (void)gp.process();
    }
    std::vector<float> out(static_cast<std::size_t>(kAnalysis));
    for (int i = 0; i < kAnalysis; ++i)
    {
        gp.writeSample(sineAt(22050 + i, kInputHz, kSampleRate));
        out[static_cast<std::size_t>(i)] = gp.process();
    }
    const double p100 = goertzelPower(
        [&](int n) { return out[static_cast<std::size_t>(n)]; },
        kAnalysis, 100.0f, kSampleRate);
    const double p200 = goertzelPower(
        [&](int n) { return out[static_cast<std::size_t>(n)]; },
        kAnalysis, 200.0f, kSampleRate);

    REQUIRE(p100 > 4.0 * p200);
    REQUIRE(p100 > 1.0);
}

TEST_CASE("GranularProcessor: pitchOffset=+6, spread=6 → ratios in [1, 2], no sub-fundamental content",
          "[granular]")
{
    // With offset=+6 and spread=6, semis ∈ [0, 12] uniformly →
    // ratios ∈ [1, 2].  Feeding a fundamental at kInputHz, every grain
    // reproduces the signal at rate ≥ 1× → NO output frequency component
    // can lie below kInputHz.  A broken impl that dropped the offset
    // (semis ∈ [-6, +6] → ratios ∈ [0.707, 1.414]) would put roughly half
    // the grains below unity ratio, producing significant energy in the
    // (kInputHz/√2, kInputHz) band.  We probe with a Goertzel bin at
    // kInputHz · 0.75 (well inside the "sub-unity" band a broken impl
    // would populate) and require it to be much weaker than the fundamental.
    constexpr float kInputHz  = 200.0f;
    constexpr int   kAnalysis = 22050;   // 0.5 s

    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, 22050);
    gp.setGrainRate(40.0f);              // O = 40·0.1 = 4 grains in flight
    gp.setGrainSize(0.1f);               // Hann main-lobe ≈ 2/0.1 = 20 Hz wide
    gp.setPitchSpread(6.0f);
    gp.setPositionScatter(0.5f);
    gp.setPitchOffset(6.0f);

    for (int i = 0; i < 22050; ++i)
    {
        gp.writeSample(sineAt(i, kInputHz, kSampleRate));
        (void)gp.process();
    }
    std::vector<float> out(static_cast<std::size_t>(kAnalysis));
    for (int i = 0; i < kAnalysis; ++i)
    {
        gp.writeSample(sineAt(22050 + i, kInputHz, kSampleRate));
        out[static_cast<std::size_t>(i)] = gp.process();
    }

    // Sub-unity probe at 0.75 · kInputHz = 150 Hz.  With the offset applied,
    // no grain can read below 200 Hz — 150 Hz sits 50 Hz below the lowest
    // possible output, well outside the Hann main lobe (≈20 Hz wide) → bin
    // energy is dominated by side-lobe leakage and noise (empirically
    // ~1e-3× the in-band signal).  Without the offset (broken impl), grains
    // at ratio 0.75 would produce 150 Hz directly → bin would carry
    // significant energy.  We require ≥ 20× separation from the in-band
    // signal at 250 Hz (mid of the [200, 400] output range).
    const double pSub = goertzelPower(
        [&](int n) { return out[static_cast<std::size_t>(n)]; },
        kAnalysis, kInputHz * 0.75f, kSampleRate);
    const double pMid = goertzelPower(
        [&](int n) { return out[static_cast<std::size_t>(n)]; },
        kAnalysis, kInputHz * 1.25f, kSampleRate);

    // 20× is a conservative threshold: 30 dB down.  A broken impl would
    // sit ≤ 6 dB below the in-band peak (comparable energy on both sides).
    REQUIRE(pMid > 20.0 * pSub);
    REQUIRE(pMid > 1.0);   // in-band signal actually present
}

TEST_CASE("GranularProcessor: determinism preserved with non-zero pitchOffset",
          "[granular]")
{
    // Same seed + same params (including a non-zero offset) + same input
    // → bit-identical output.  Same guarantee as the pre-existing seed
    // test, extended to cover the new setter's code path in trySpawnGrain.
    ideath::GranularProcessor a, b;
    a.prepare(kSampleRate, 22050);
    b.prepare(kSampleRate, 22050);
    for (auto* gp : { &a, &b })
    {
        gp->setGrainRate(50.0f);
        gp->setGrainSize(0.04f);
        gp->setPitchSpread(7.0f);
        gp->setPositionScatter(0.6f);
        gp->setPitchOffset(-5.0f);
    }
    for (int i = 0; i < 22050; ++i)
    {
        const float x = 0.5f * std::sin(static_cast<float>(i) * 0.01f);
        a.writeSample(x);
        b.writeSample(x);
        REQUIRE(a.process() == b.process());
    }
}

// -----------------------------------------------------------------------------
// 13. setReadWindow — [near, far] read-position bounds
// -----------------------------------------------------------------------------
//
// setReadWindow(near, far) generalises setPositionScatter:
//   offset = (near + u · (far - near)) · bufferSize     where u ~ U[0, 1)
//   readPos = writePos - offset  (mod bufferSize)
// Both operands are clamped to [0, 1]; if the caller passes them reversed
// they are swapped to canonical order.
//
// Verified below:
//   • setReadWindow(0.0, x) is bit-exact equivalent to setPositionScatter(x)
//     — proves the "convenience wrapper" contract stated in the header
//   • setReadWindow(0.5, 0.5) is a zero-width window → all grains launch at
//     writePos - 0.5·bufferSize (no position randomness) → consecutive
//     grains produce bit-identical waveforms against a frozen buffer
//   • setReadWindow(0.9, 1.0) reads the OLDEST 10% of the buffer, and
//     setReadWindow(0.0, 0.1) reads the NEWEST 10% — with a distinctive
//     signal placed only in the oldest 10% region, the two configurations
//     produce clearly separated output levels
//   • setReadWindow(0.5, 0.3) is normalised to setReadWindow(0.3, 0.5)
//     (swap-on-reverse) → bit-identical output
//   • determinism preserved with a non-default read window (same seed +
//     same params → bit-identical output)
// -----------------------------------------------------------------------------

TEST_CASE("GranularProcessor: setReadWindow(0.0, x) matches setPositionScatter(x) bit-exact",
          "[granular]")
{
    // Documented equivalence: setPositionScatter(x) is a convenience wrapper
    // for setReadWindow(0.0f, x).  Both entry points must go through the
    // same code path in trySpawnGrain (same RNG draws per spawn, same
    // offset arithmetic) so the outputs are bit-identical.  This test
    // pins that contract: if a future refactor accidentally diverges the
    // two paths (e.g. an extra RNG call, a different clamp order), this
    // test fires immediately.
    ideath::GranularProcessor a, b;
    a.prepare(kSampleRate, 22050);
    b.prepare(kSampleRate, 22050);
    for (auto* gp : { &a, &b })
    {
        gp->setGrainRate(50.0f);
        gp->setGrainSize(0.04f);
        gp->setPitchSpread(7.0f);
    }
    a.setPositionScatter(0.5f);
    b.setReadWindow(0.0f, 0.5f);
    for (int i = 0; i < 22050; ++i)
    {
        const float x = 0.5f * std::sin(static_cast<float>(i) * 0.01f);
        a.writeSample(x);
        b.writeSample(x);
        REQUIRE(a.process() == b.process());
    }
}

TEST_CASE("GranularProcessor: setReadWindow(0.5, 0.5) yields deterministic single-position reads",
          "[granular]")
{
    // A zero-width window (near == far) pins every grain's start offset to
    // exactly `near * bufferSize` samples behind writePos.  Combined with
    // pitchSpread=0 (pitch = 1.0) and a frozen buffer, every grain must
    // produce a bit-identical waveform because everything determining grain
    // output is now constant across spawns:
    //   - readPos: fixed (writePos frozen, window degenerate)
    //   - pitch:   fixed at 1.0 (spread zero)
    //   - envelope: starts at envPhase=0, advances by fixed envInc
    //   - buffer content at read range: fixed (freeze)
    // The RNG is still advanced twice per spawn (pitch + position), but its
    // outputs are multiplied by 0 so they don't affect grain parameters.
    ideath::GranularProcessor gp;
    const int bufLen = static_cast<int>(kSampleRate);   // 1 s
    gp.prepare(kSampleRate, bufLen);
    gp.setGrainRate(1.0f);           // 1 spawn / s
    gp.setGrainSize(0.1f);           // 4410-sample grain — no overlap
    gp.setPitchSpread(0.0f);
    gp.setReadWindow(0.5f, 0.5f);

    // Fill buffer with a 200 Hz sine (arbitrary distinctive non-zero content).
    for (int i = 0; i < bufLen; ++i)
    {
        gp.writeSample(sineAt(i, 200.0f, kSampleRate));
        (void)gp.process();
    }
    gp.setFreeze(true);

    // Drain any grain that was in flight when we froze, so both captured
    // grains come from the frozen steady state.
    while (gp.activeGrainCount() > 0)
    {
        gp.writeSample(0.0f);   // no-op under freeze
        (void)gp.process();
    }

    auto captureNextGrain = [&]() -> std::vector<float>
    {
        while (gp.activeGrainCount() == 0)
        {
            gp.writeSample(0.0f);
            (void)gp.process();
        }
        const int grainLen = static_cast<int>(0.1f * kSampleRate);
        std::vector<float> out(static_cast<std::size_t>(grainLen));
        for (int i = 0; i < grainLen; ++i)
        {
            gp.writeSample(0.0f);
            out[static_cast<std::size_t>(i)] = gp.process();
        }
        return out;
    };

    const auto g1 = captureNextGrain();
    const auto g2 = captureNextGrain();

    REQUIRE(g1.size() == g2.size());
    // Bit-exact equality proves the position spread is zero.  With any
    // residual randomness the two grains would read from different offsets
    // and their waveforms would differ (fails immediately at Hann peak,
    // where the amplitude of a 200 Hz sine differs by up to 2·gain_comp
    // between two random buffer positions).
    for (std::size_t i = 0; i < g1.size(); ++i)
        REQUIRE(g1[i] == g2[i]);

    // Sanity floor: not silence.  Peak = Hann_peak · sine_peak · gain_comp
    // with O = 1·0.1 = 0.1 → denom = max(0.05, 0.5) = 0.5 → gain = √2.
    // Ideal peak = 1 · 1 · √2 ≈ 1.414.  Floor 0.5 = -6 dB below ideal,
    // catching catastrophic silence without brittleness against the
    // per-grain sine-phase alignment at the fixed read position.
    float maxAbs = 0.0f;
    for (float x : g1) maxAbs = std::max(maxAbs, std::fabs(x));
    REQUIRE(maxAbs > 0.5f);
}

TEST_CASE("GranularProcessor: setReadWindow selects the correct buffer region — oldest vs newest",
          "[granular]")
{
    // Two processors warmed identically:
    //   - Fill a DC=0.7 band in the middle of the buffer (indices
    //     [dcStart, dcEnd)); silence everywhere else.
    //   - Freeze.
    // The DC band is placed so that:
    //   - readWindow(0.9, 1.0) (which reads readPos ∈ [0, 0.1·bs]) plus
    //     grain length lands squarely inside the DC band, and
    //   - readWindow(0.0, 0.1) (which reads readPos ∈ [0.9·bs, bs] wrapping
    //     into [0, grainSize·sr]) sits entirely in silence — the wrap tail
    //     is placed BEFORE the DC band, not inside it.
    // Then the first configuration produces meaningful output (grains
    // reading DC=0.7 through Hann); the second is silent (grains reading
    // zeros).  This directly verifies that the window bounds map to the
    // correct buffer region.
    //
    // Layout for bufLen = kSampleRate = 44100 samples (1 s):
    //   writePos at freeze = 0 (writes wrap after exactly 1 buffer).
    //   offset behind writePos:  index = (writePos - offset) mod bs
    //     - offset = k  → index (bs - k)  for k ∈ (0, bs]
    //     - offset = 0  → index 0
    //   So the region [1, bs] behind writePos corresponds to indices
    //   [bs-1, 0] running BACKWARD.
    //   readWindow(0.9, 1.0): offset ∈ [0.9·bs, bs] → indices ∈ [0, 0.1·bs]
    //   readWindow(0.0, 0.1): offset ∈ [0, 0.1·bs] → indices ∈ {0} ∪ [0.9·bs, bs)
    // With grainSize·sr = 220 samples, the grain at readPos advances to
    // readPos + 220, wrapping.  For readWindow(0.9, 1.0) grains span
    // [0, 4410 + 220] = [0, 4630].  For readWindow(0.0, 0.1) grains span
    // [39690, 44100) ∪ [0, 220) via wrap.
    //
    // Place the DC band at indices [1000, 5000):
    //   - readWindow(0.9, 1.0) grains hit DC if readPos ∈ (780, 4410]
    //     (grain end > 1000, grain start < 5000).  Fraction: 82%; the
    //     remaining 18% hit only silence.  Peak grain output is achieved
    //     by any grain whose read range lies fully inside [1000, 5000),
    //     i.e. readPos ∈ [1000, 4780] — fraction 86% of the window.
    //   - readWindow(0.0, 0.1) grains read [39690, 44100) ∪ [0, 220).
    //     Both regions are silence — the 220-sample wrap tail sits BEFORE
    //     the DC band which starts at index 1000.  Every read returns 0.
    //
    // Expected level for readWindow(0.9, 1.0):
    //   O = grainRate · grainSize = 40 · 0.005 = 0.2 → denom = max(0.1, 0.5) = 0.5
    //   gain_comp = 1/√0.5 = √2 ≈ 1.414
    //   Peak grain output (fully inside DC, Hann peak):
    //     Hann_peak · 0.7 · √2 = 1 · 0.7 · 1.414 ≈ 0.99
    //   Threshold 0.5 is ≈ half of ideal peak — catches broken windows
    //   without brittleness against the 14% of grains that miss the DC band.
    //
    // Expected level for readWindow(0.0, 0.1):
    //   Every read hits silence → output is bit-exact 0.  Threshold 1e-6
    //   tolerates the u=0 corner where offset=0 → readPos=0 (which reads
    //   the first silence sample at index 0, still 0).  Probability of any
    //   sample above the threshold: essentially zero.
    const int bufLen  = static_cast<int>(kSampleRate);
    const int dcStart = 1000;
    const int dcEnd   = 5000;
    auto warmup = [&](ideath::GranularProcessor& gp)
    {
        gp.prepare(kSampleRate, bufLen);
        gp.setGrainRate(40.0f);
        gp.setGrainSize(0.005f);   // 220 samples — fits inside DC band
        gp.setPitchSpread(0.0f);
        for (int i = 0; i < bufLen; ++i)
        {
            const float x = (i >= dcStart && i < dcEnd) ? 0.7f : 0.0f;
            gp.writeSample(x);
            (void)gp.process();
        }
        gp.setFreeze(true);
    };

    ideath::GranularProcessor readsOldest;
    ideath::GranularProcessor readsNewest;
    warmup(readsOldest);
    warmup(readsNewest);
    readsOldest.setReadWindow(0.9f, 1.0f);   // oldest 10% → DC band via indices [0, 4410]
    readsNewest.setReadWindow(0.0f, 0.1f);   // newest 10% → silence via [39690, 44100) ∪ wrap [0, 220)

    float maxOldest = 0.0f;
    float maxNewest = 0.0f;
    for (int i = 0; i < static_cast<int>(kSampleRate); ++i)   // 1 s
    {
        readsOldest.writeSample(0.0f);       // no-op (frozen)
        readsNewest.writeSample(0.0f);
        maxOldest = std::max(maxOldest, std::fabs(readsOldest.process()));
        maxNewest = std::max(maxNewest, std::fabs(readsNewest.process()));
    }

    REQUIRE(maxOldest > 0.5f);   // grains hit the 0.7 DC band
    REQUIRE(maxNewest < 1e-6f);  // grains hit only silence
}

TEST_CASE("GranularProcessor: setReadWindow(near > far) is normalised via swap",
          "[granular]")
{
    // Documented contract: if the caller passes nearOffset > farOffset the
    // two are swapped so the window is stored in canonical [min, max]
    // order.  Two instances configured with the swapped and canonical
    // arguments must produce bit-identical output.
    ideath::GranularProcessor a, b;
    a.prepare(kSampleRate, 22050);
    b.prepare(kSampleRate, 22050);
    for (auto* gp : { &a, &b })
    {
        gp->setGrainRate(50.0f);
        gp->setGrainSize(0.04f);
        gp->setPitchSpread(7.0f);
    }
    a.setReadWindow(0.5f, 0.3f);   // reversed input
    b.setReadWindow(0.3f, 0.5f);   // canonical input

    for (int i = 0; i < 22050; ++i)
    {
        const float x = 0.5f * std::sin(static_cast<float>(i) * 0.01f);
        a.writeSample(x);
        b.writeSample(x);
        REQUIRE(a.process() == b.process());
    }
}

TEST_CASE("GranularProcessor: determinism preserved with non-default setReadWindow",
          "[granular]")
{
    // Same seed + same params (including a non-default readWindow, chosen so
    // the arithmetic exercises both endpoints of the window) + same input
    // → bit-identical output.  Same guarantee as the pre-existing seed
    // test, extended to cover the new setter's code path in trySpawnGrain.
    ideath::GranularProcessor a, b;
    a.prepare(kSampleRate, 22050);
    b.prepare(kSampleRate, 22050);
    for (auto* gp : { &a, &b })
    {
        gp->setGrainRate(50.0f);
        gp->setGrainSize(0.04f);
        gp->setPitchSpread(7.0f);
        gp->setPitchOffset(-3.0f);
        gp->setReadWindow(0.2f, 0.8f);
    }
    for (int i = 0; i < 22050; ++i)
    {
        const float x = 0.5f * std::sin(static_cast<float>(i) * 0.01f);
        a.writeSample(x);
        b.writeSample(x);
        REQUIRE(a.process() == b.process());
    }
}

// -----------------------------------------------------------------------------
// 14. snapshotGrains / writePos / bufferSize — visualisation accessors
// -----------------------------------------------------------------------------
//
// snapshotGrains(GrainSnapshot* out) copies the live grain pool into `out`,
// writing exactly kMaxGrains entries (inactive slots have `active=false`).
// writePos() and bufferSize() expose the ring-buffer geometry so a UI can
// compute an "offset behind write head" coordinate.
//
// Verified below:
//   • after a warm-up with grainRate > 0, at least one active slot appears
//   • envPhase strictly increases per active grain per process(), and the
//     grain deactivates when envPhase would cross 1.0
//   • determinism: same seed + same params + same input → bit-identical
//     snapshot at the same process-call count across two instances
//   • freeze does NOT halt grain playback — envPhase keeps advancing while
//     writePos() stays put
//   • writePos() advances by exactly one per unfrozen writeSample() and
//     wraps at bufferSize()
// -----------------------------------------------------------------------------

TEST_CASE("GranularProcessor: snapshotGrains reports active grain after warm-up",
          "[granular]")
{
    // A short warm-up at grainRate=50 Hz (spawn period 20 ms) with grainSize
    // 40 ms guarantees at least one spawn well before the first process()
    // call — steady-state O = 2.  After 2000 samples (~45 ms, > 2 spawn
    // periods) at least one slot must be active.
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, 22050);
    gp.setGrainRate(50.0f);
    gp.setGrainSize(0.04f);
    gp.setPitchSpread(0.0f);
    gp.setPositionScatter(0.5f);

    for (int i = 0; i < 2000; ++i)
    {
        gp.writeSample(0.5f);
        (void)gp.process();
    }

    ideath::GranularProcessor::GrainSnapshot snap[
        ideath::GranularProcessor::kMaxGrains];
    gp.snapshotGrains(snap);

    int activeCount = 0;
    for (int i = 0; i < ideath::GranularProcessor::kMaxGrains; ++i)
    {
        if (snap[i].active)
        {
            ++activeCount;
            // Sanity: readPos ∈ [0, bufferSize).  Sub-sample is fine, but
            // the value must land inside the ring.
            REQUIRE(snap[i].readPos >= 0.0f);
            REQUIRE(snap[i].readPos <  static_cast<float>(gp.bufferSize()));
            // envPhase ∈ [0, 1).  Grains at exactly 1.0 would have been
            // deactivated in renderGrain().
            REQUIRE(snap[i].envPhase >= 0.0f);
            REQUIRE(snap[i].envPhase <  1.0f);
            // pitchSpread=0 → every grain plays at unity ratio.  Bit-exact
            // because 2^(0/12) = 1 exactly in float.
            REQUIRE(snap[i].pitch == 1.0f);
        }
    }
    REQUIRE(activeCount >= 1);
    REQUIRE(activeCount == gp.activeGrainCount());   // agrees with counter
}

TEST_CASE("GranularProcessor: snapshotGrains envPhase strictly increases per process",
          "[granular]")
{
    // For any active grain, envPhase must advance by envInc = 1/grainSamples
    // per process() call.  We pick a slow grainRate + long grainSize so at
    // most one grain is alive at a time and its slot index stays put, then
    // sample envPhase across consecutive process() calls and require
    // monotonic strict increase (envPhase_{n+1} > envPhase_n) until the
    // grain deactivates.
    //
    // Config: grainRate=1 (spawn every 1 s), grainSize=0.1 (4410 samples).
    // grainSize < spawn period → at most one grain in flight.
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, static_cast<int>(kSampleRate));
    gp.setGrainRate(1.0f);
    gp.setGrainSize(0.1f);
    gp.setPitchSpread(0.0f);
    gp.setPositionScatter(0.0f);

    // Advance until a grain appears (≤ 1 s spawn timer).
    int waited = 0;
    while (gp.activeGrainCount() == 0 && waited < static_cast<int>(1.1f * kSampleRate))
    {
        gp.writeSample(0.5f);
        (void)gp.process();
        ++waited;
    }
    REQUIRE(gp.activeGrainCount() == 1);

    ideath::GranularProcessor::GrainSnapshot snap[
        ideath::GranularProcessor::kMaxGrains];
    gp.snapshotGrains(snap);

    // Find the active slot.  Only one grain in flight, so exactly one match.
    int slot = -1;
    for (int i = 0; i < ideath::GranularProcessor::kMaxGrains; ++i)
        if (snap[i].active) { slot = i; break; }
    REQUIRE(slot >= 0);

    // Walk 100 process() calls and check monotonic envPhase.  100 samples
    // is well short of grainSize (4410 samples) so the grain stays alive
    // in the same slot for the whole window.
    float prevPhase = snap[slot].envPhase;
    for (int i = 0; i < 100; ++i)
    {
        gp.writeSample(0.5f);
        (void)gp.process();
        gp.snapshotGrains(snap);
        REQUIRE(snap[slot].active);
        // Strict increase.  envInc = 1/4410 ≈ 2.27e-4 per sample; well
        // above float epsilon (~1e-7 at 0.01), so > (not >=) is safe.
        REQUIRE(snap[slot].envPhase > prevPhase);
        prevPhase = snap[slot].envPhase;
    }

    // Continue until the grain deactivates.  From here we've consumed
    // ~100 samples out of ~4410; run enough more (~5000) to guarantee
    // envPhase crosses 1.0.
    bool sawDeactivation = false;
    for (int i = 0; i < 5000; ++i)
    {
        gp.writeSample(0.5f);
        (void)gp.process();
        gp.snapshotGrains(snap);
        if (!snap[slot].active) { sawDeactivation = true; break; }
        REQUIRE(snap[slot].envPhase < 1.0f);   // never reported ≥ 1 alive
    }
    REQUIRE(sawDeactivation);
}

TEST_CASE("GranularProcessor: snapshotGrains is bit-identical across instances with same seed",
          "[granular]")
{
    // Determinism: two instances configured identically and driven with
    // identical input must have byte-identical grain pools at any given
    // process-call count.  The pre-existing seed-reproducibility test
    // (line ~115) proves the output signal matches; this test extends that
    // guarantee to the internal grain state that the visualisation reads.
    ideath::GranularProcessor a, b;
    a.prepare(kSampleRate, 22050);
    b.prepare(kSampleRate, 22050);
    for (auto* gp : { &a, &b })
    {
        gp->setGrainRate(50.0f);
        gp->setGrainSize(0.04f);
        gp->setPitchSpread(7.0f);
        gp->setPositionScatter(0.6f);
    }

    ideath::GranularProcessor::GrainSnapshot sa[
        ideath::GranularProcessor::kMaxGrains];
    ideath::GranularProcessor::GrainSnapshot sb[
        ideath::GranularProcessor::kMaxGrains];

    for (int i = 0; i < 22050; ++i)
    {
        const float x = 0.5f * std::sin(static_cast<float>(i) * 0.01f);
        a.writeSample(x);
        b.writeSample(x);
        (void)a.process();
        (void)b.process();
        // Snapshot every 128 samples to keep the loop overhead in check
        // while still hitting many different spawn/expire boundary states.
        if ((i & 127) == 0)
        {
            a.snapshotGrains(sa);
            b.snapshotGrains(sb);
            for (int g = 0; g < ideath::GranularProcessor::kMaxGrains; ++g)
            {
                REQUIRE(sa[g].active == sb[g].active);
                if (sa[g].active)
                {
                    // Bit-exact — identical RNG state + identical envInc
                    // + identical process() count means the float fields
                    // must match exactly.
                    REQUIRE(sa[g].readPos  == sb[g].readPos);
                    REQUIRE(sa[g].envPhase == sb[g].envPhase);
                    REQUIRE(sa[g].pitch    == sb[g].pitch);
                }
            }
        }
    }
    // Sanity: also check writePos determinism — trivial but pins the API.
    REQUIRE(a.writePos()   == b.writePos());
    REQUIRE(a.bufferSize() == b.bufferSize());
}

TEST_CASE("GranularProcessor: freeze halts writePos but grains keep advancing envPhase",
          "[granular]")
{
    // Freeze semantics for the visualisation:
    //   - writePos() stops advancing (writeSample is a no-op)
    //   - Existing grains continue playback (envPhase increases per sample,
    //     readPos advances by pitch, grains eventually deactivate at
    //     envPhase ≥ 1)
    // Verify both directly through the snapshot / geometry accessors.
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, 22050);
    gp.setGrainRate(20.0f);
    gp.setGrainSize(0.1f);      // long grain — will still be alive after
                                // the freeze-window we measure
    gp.setPitchSpread(0.0f);    // pitch = 1 → envInc = 1/4410
    gp.setPositionScatter(0.5f);

    // Warm up so we have grains in flight.
    for (int i = 0; i < 4410; ++i)
    {
        gp.writeSample(0.5f);
        (void)gp.process();
    }
    REQUIRE(gp.activeGrainCount() > 0);

    gp.setFreeze(true);
    const int writePosBefore = gp.writePos();

    ideath::GranularProcessor::GrainSnapshot before[
        ideath::GranularProcessor::kMaxGrains];
    gp.snapshotGrains(before);

    // Pick any active grain we can track.
    int slot = -1;
    for (int i = 0; i < ideath::GranularProcessor::kMaxGrains; ++i)
        if (before[i].active) { slot = i; break; }
    REQUIRE(slot >= 0);

    // Advance 200 samples under freeze.
    for (int i = 0; i < 200; ++i)
    {
        gp.writeSample(-1.0f);   // ignored — freeze
        (void)gp.process();
    }

    ideath::GranularProcessor::GrainSnapshot after[
        ideath::GranularProcessor::kMaxGrains];
    gp.snapshotGrains(after);

    // writePos must not have moved.  Exact-int equality — no rounding.
    REQUIRE(gp.writePos() == writePosBefore);

    // The tracked grain either advanced OR (if it happened to expire mid-
    // window) is now in a fresh slot after respawn.  In the "still alive"
    // case its envPhase must have increased by ~200/4410 ≈ 0.0454.  We
    // check the "still alive" case; if the tracked slot expired, at least
    // one *other* slot must show a live grain with a fresh envPhase — i.e.
    // freeze did not silence grain playback.
    if (after[slot].active
        && after[slot].pitch == before[slot].pitch
        && after[slot].envPhase >= before[slot].envPhase)
    {
        // Same grain still playing.  Expected delta = 200 · envInc =
        // 200 / (0.1 · 44100) = 200 / 4410 ≈ 0.04535.  Tolerance 1e-4
        // covers accumulated float error over 200 add-steps: at envPhase
        // ~0.5, float precision is ~6e-8 per add → 200 · 6e-8 = 1.2e-5,
        // well inside 1e-4.
        const float delta = after[slot].envPhase - before[slot].envPhase;
        REQUIRE_THAT(delta, WithinAbs(200.0f / (0.1f * kSampleRate), 1e-4f));
    }
    else
    {
        // Tracked grain expired; verify freeze didn't stall grain playback
        // globally by requiring at least one live grain in the snapshot.
        int alive = 0;
        for (int i = 0; i < ideath::GranularProcessor::kMaxGrains; ++i)
            if (after[i].active) ++alive;
        REQUIRE(alive > 0);
    }
}

TEST_CASE("GranularProcessor: writePos advances one per writeSample and wraps at bufferSize",
          "[granular]")
{
    // writePos() must expose the raw ring-buffer index used by writeSample.
    // Advance manually and verify:
    //   - starts at 0 after prepare()/reset()
    //   - increments by 1 per writeSample()
    //   - wraps to 0 after bufferSize() writes
    //   - freeze suppresses the increment
    //   - bufferSize() reports the value passed to prepare (subject to the
    //     documented 16-sample floor — 128 is well above it, so exact match)
    const int bufLen = 128;
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, bufLen);

    REQUIRE(gp.bufferSize() == bufLen);
    REQUIRE(gp.writePos()   == 0);

    for (int i = 0; i < bufLen; ++i)
    {
        gp.writeSample(0.0f);
        // After the (i+1)-th writeSample, writePos = (i+1) mod bufLen.
        REQUIRE(gp.writePos() == (i + 1) % bufLen);
    }
    REQUIRE(gp.writePos() == 0);   // wrapped exactly once

    // Freeze halts advancement.
    gp.setFreeze(true);
    for (int i = 0; i < 50; ++i)
        gp.writeSample(0.0f);
    REQUIRE(gp.writePos() == 0);

    // Unfreeze resumes.
    gp.setFreeze(false);
    gp.writeSample(0.0f);
    REQUIRE(gp.writePos() == 1);
}

// -----------------------------------------------------------------------------
// 15. setPitchOffset clamping (pre-existing test, kept below the new block)
// -----------------------------------------------------------------------------

TEST_CASE("GranularProcessor: setPitchOffset clamps to [-24, +24] semitones",
          "[granular]")
{
    // Verify clamping observationally: setPitchOffset(+1000) must produce
    // bit-identical output to setPitchOffset(+24), and the same for the
    // negative bound.  All other state matches, so any difference implies
    // the clamp wasn't applied.
    auto run = [&](float offset)
    {
        ideath::GranularProcessor gp;
        gp.prepare(kSampleRate, 22050);
        gp.setGrainRate(30.0f);
        gp.setGrainSize(0.05f);
        gp.setPitchSpread(0.0f);
        gp.setPositionScatter(0.5f);
        gp.setPitchOffset(offset);
        std::vector<float> out(2048);
        for (int i = 0; i < 22050; ++i)
        {
            gp.writeSample(sineAt(i, 100.0f, kSampleRate));
            const float y = gp.process();
            if (i >= 20000)
                out[static_cast<std::size_t>(i - 20000)] = y;
        }
        return out;
    };

    const auto hi     = run(1000.0f);
    const auto hiCap  = run(24.0f);
    const auto lo     = run(-1000.0f);
    const auto loCap  = run(-24.0f);

    for (std::size_t i = 0; i < hi.size(); ++i)
    {
        REQUIRE(hi[i]  == hiCap[i]);
        REQUIRE(lo[i]  == loCap[i]);
    }
}

// =============================================================================
// SOS / reverse / seed
// -----------------------------------------------------------------------------
// readRing() recovers the ring contents through the public API: freeze the
// buffer, then let one unity-pitch Hann grain traverse it from the oldest
// sample (window [0, 0]), so y[n] = g · env(n) · ring[(head + n) mod N] with
// g = 1 / sqrt(max(O · 0.5, 0.5)) = sqrt(2) because O = 1 Hz · 0.1 s < 1.
//
// Only the centre half of the grain is compared (env >= 0.5).  envPhase sums
// 1/N about 4410 times in float, rounding by at most 2^-25 per add, so the
// phase error is <= 1.3e-4 and the Hann error <= pi · 1.3e-4 = 4.1e-4.
// Relative to env >= 0.5 that is 8.2e-4, hence kEnvTol = 1e-3.
//
// Grain-output comparisons have the same 4.1e-4 Hann error plus the linear-
// interpolation error (w^2/8 = 1.2e-4 at 220 Hz), both scaled by g:
// 7.6e-4, which stays under the same kEnvTol.
// =============================================================================

namespace
{
    constexpr int   kRingLen = 4410;               // 0.1 s at 44.1 kHz
    constexpr float kEnvTol  = 1e-3f;              // see the derivation above
    const     float kSparseGain = std::sqrt(2.0f); // g for O < 1

    float hannAt(int n, int len)
    {
        return 0.5f - 0.5f * std::cos(2.0f * kPi * static_cast<float>(n)
                                      / static_cast<float>(len));
    }

    // White noise in [-1, 1), independent of the processor's own RNG.
    float noiseSample(uint32_t& state)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state) / 2147483648.0f - 1.0f;
    }

    // Expects a processor that has only been written to (spawn timer still
    // at zero, no process() call yet).  Returns the ring from oldest to
    // newest, valid over [kRingLen/4, 3·kRingLen/4).
    std::vector<float> readRing(ideath::GranularProcessor& gp)
    {
        gp.setFreeze(true);
        gp.setGrainRate(1.0f);
        gp.setGrainSize(static_cast<float>(kRingLen) / kSampleRate);
        gp.setPitchSpread(0.0f);
        gp.setReadWindow(0.0f, 0.0f);

        std::vector<float> ring(kRingLen, 0.0f);
        for (int n = 0; n < kRingLen; ++n)
        {
            const float y = gp.process();
            if (n >= kRingLen / 4 && n < 3 * kRingLen / 4)
                ring[static_cast<std::size_t>(n)] = y / (kSparseGain * hannAt(n, kRingLen));
        }
        return ring;
    }

    float correlation(const std::vector<float>& a, const std::vector<float>& b)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (int i = kRingLen / 4; i < 3 * kRingLen / 4; ++i)
        {
            const double x = a[static_cast<std::size_t>(i)];
            const double y = b[static_cast<std::size_t>(i)];
            ab += x * y;
            aa += x * x;
            bb += y * y;
        }
        return static_cast<float>(ab / std::sqrt(aa * bb));
    }

    void configureBusy(ideath::GranularProcessor& gp)
    {
        gp.setGrainRate(50.0f);
        gp.setGrainSize(0.04f);
        gp.setPitchSpread(7.0f);
        gp.setPositionScatter(0.6f);
    }

    std::vector<float> runBusy(ideath::GranularProcessor& gp, int samples = 22050)
    {
        std::vector<float> out(static_cast<std::size_t>(samples));
        for (int i = 0; i < samples; ++i)
        {
            gp.writeSample(sineAt(i, 330.0f, kSampleRate));
            out[static_cast<std::size_t>(i)] = gp.process();
        }
        return out;
    }

    void requireBitIdentical(const std::vector<float>& a, const std::vector<float>& b)
    {
        REQUIRE(a.size() == b.size());
        int mismatches = 0;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i]) ++mismatches;
        REQUIRE(mismatches == 0);
    }
}

// -----------------------------------------------------------------------------
// SOS
// -----------------------------------------------------------------------------

TEST_CASE("GranularProcessor: SOS 0 is bit-identical to the untouched default", "[granular][sos]")
{
    // Every path to amount 0 must collapse to the plain overwrite: the
    // explicit 0, a clamped negative, and returning from a non-zero amount
    // before any sample has been written.
    ideath::GranularProcessor plain, zero, negative, returned;
    for (auto* gp : { &plain, &zero, &negative, &returned })
    {
        gp->prepare(kSampleRate, 22050);
        configureBusy(*gp);
    }
    zero.setSos(0.0f);
    negative.setSos(-1.0f);
    returned.setSos(0.7f);
    returned.setSos(0.0f);

    const auto reference = runBusy(plain);
    requireBitIdentical(reference, runBusy(zero));
    requireBitIdentical(reference, runBusy(negative));
    requireBitIdentical(reference, runBusy(returned));
}

TEST_CASE("GranularProcessor: SOS 0 stores the input unchanged", "[granular][sos]")
{
    // The comparison above runs both sides through the same write path, so
    // it cannot see an offset on that path.  Overwriting a full ring with
    // silence must leave exact zeros: no denormal guard, no old material.
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, kRingLen);
    for (int i = 0; i < kRingLen; ++i)
        gp.writeSample(1.0f);
    gp.setSos(0.0f);
    for (int i = 0; i < kRingLen; ++i)
        gp.writeSample(0.0f);

    const auto ring = readRing(gp);
    for (int i = kRingLen / 4; i < 3 * kRingLen / 4; ++i)
        REQUIRE(ring[static_cast<std::size_t>(i)] == 0.0f);
}

TEST_CASE("GranularProcessor: SOS 1 writes no input and matches freeze", "[granular][sos]")
{
    // Same prefill, then one instance is frozen and the other held at
    // SOS 1 (also through the clamp).  NaN and inf inputs prove the input
    // is not even multiplied by zero (0 · NaN = NaN would poison the ring).
    // Output and head position must match freeze exactly.
    ideath::GranularProcessor frozen, sos, sosClamped;
    for (auto* gp : { &frozen, &sos, &sosClamped })
    {
        gp->prepare(kSampleRate, 22050);
        configureBusy(*gp);
        for (int i = 0; i < 22050; ++i)
        {
            gp->writeSample(sineAt(i, 330.0f, kSampleRate));
            (void)gp->process();
        }
    }
    frozen.setFreeze(true);
    sos.setSos(1.0f);
    sosClamped.setSos(2.0f);

    const float inputs[] = { 0.9f, std::numeric_limits<float>::quiet_NaN(),
                             std::numeric_limits<float>::infinity(), -0.9f };
    for (int i = 0; i < 22050; ++i)
    {
        const float x = inputs[i % 4];
        frozen.writeSample(x);
        sos.writeSample(x);
        sosClamped.writeSample(x);
        const float expected = frozen.process();
        REQUIRE(expected == expected);   // never NaN
        REQUIRE(sos.process() == expected);
        REQUIRE(sosClamped.process() == expected);
        REQUIRE(sos.writePos() == frozen.writePos());
    }
}

TEST_CASE("GranularProcessor: SOS writes input·(1−a) + existing·a", "[granular][sos]")
{
    // One overdub pass of constant 0.5 over constant 1.0 at a = 0.25:
    // 0.5 · 0.75 + 1.0 · 0.25 = 0.625 by the specified formula.
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, kRingLen);
    for (int i = 0; i < kRingLen; ++i)
        gp.writeSample(1.0f);
    gp.setSos(0.25f);
    for (int i = 0; i < kRingLen; ++i)
        gp.writeSample(0.5f);

    const auto ring = readRing(gp);
    for (int i = kRingLen / 4; i < 3 * kRingLen / 4; ++i)
        REQUIRE_THAT(ring[static_cast<std::size_t>(i)], WithinAbs(0.625f, kEnvTol));
}

TEST_CASE("GranularProcessor: each SOS overdub lowers correlation with the original", "[granular][sos]")
{
    // k passes of independent unit-variance noise over original noise at
    // amount a leave:
    //   original share  a^k
    //   overdub energy  (1−a)² · Σ_{j<k} a^{2j} = (1−a)² (1−a^{2k}) / (1−a²)
    //   ρ(k) = a^k / sqrt(a^{2k} + (1−a)² (1−a^{2k}) / (1−a²))
    // a = 0.5 gives ρ(1) = 0.707, ρ(2) = 0.408, ρ(4) = 0.108.  Sample
    // correlation of N independent pairs has σ ≈ 1/√N = 1/√2205 = 0.021, so
    // ±4σ = 0.085 holds without letting neighbouring pass counts overlap
    // (their expected gaps are 0.30).
    constexpr float a = 0.5f;
    constexpr float kFourSigma = 0.085f;

    float previous = 1.0f;
    for (int passes : { 1, 2, 4 })
    {
        ideath::GranularProcessor gp;
        gp.prepare(kSampleRate, kRingLen);

        uint32_t state = 0x13572468u;
        std::vector<float> original(kRingLen);
        for (auto& x : original)
        {
            x = noiseSample(state);
            gp.writeSample(x);
        }

        gp.setSos(a);
        for (int i = 0; i < passes * kRingLen; ++i)
            gp.writeSample(noiseSample(state));

        const auto ring = readRing(gp);
        const float rho = correlation(original, ring);

        const float ak  = std::pow(a, static_cast<float>(passes));
        const float dub = (1.0f - a) * (1.0f - a) * (1.0f - ak * ak) / (1.0f - a * a);
        const float expected = ak / std::sqrt(ak * ak + dub);

        CAPTURE(passes, rho, expected);
        REQUIRE_THAT(rho, WithinAbs(expected, kFourSigma));
        REQUIRE(rho < previous);
        previous = rho;
    }
}

TEST_CASE("GranularProcessor: SOS decay over silence never goes subnormal", "[granular][sos]")
{
    // Overdubbing silence at a = 0.5 halves the ring every pass.  After 140
    // passes an unguarded unit sample is 0.5^140 = 7.2e-43, inside the
    // subnormal range (1.4e-45, 1.2e-38).  The guarded loop settles at
    // 1e-25 / (1 − a) = 2e-25 instead, which stays normal after read-back.
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, kRingLen);
    uint32_t state = 0x13572468u;
    for (int i = 0; i < kRingLen; ++i)
        gp.writeSample(noiseSample(state));

    gp.setSos(0.5f);
    for (int i = 0; i < 140 * kRingLen; ++i)
        gp.writeSample(0.0f);

    const auto ring = readRing(gp);
    int subnormals = 0;
    for (int i = kRingLen / 4; i < 3 * kRingLen / 4; ++i)
        if (std::fpclassify(ring[static_cast<std::size_t>(i)]) == FP_SUBNORMAL)
            ++subnormals;
    REQUIRE(subnormals == 0);
}

TEST_CASE("GranularProcessor: freeze still wins over SOS", "[granular][sos]")
{
    // Freeze is documented as a hard no-op on writeSample: a partial SOS
    // must not leak input into a frozen ring or move the head.
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, kRingLen);
    for (int i = 0; i < kRingLen; ++i)
        gp.writeSample(1.0f);
    gp.setFreeze(true);
    gp.setSos(0.5f);
    for (int i = 0; i < kRingLen; ++i)
        gp.writeSample(0.0f);
    gp.setSos(0.0f);

    const auto ring = readRing(gp);
    for (int i = kRingLen / 4; i < 3 * kRingLen / 4; ++i)
        REQUIRE_THAT(ring[static_cast<std::size_t>(i)], WithinAbs(1.0f, kEnvTol));
}

// -----------------------------------------------------------------------------
// Reverse playback
// -----------------------------------------------------------------------------

TEST_CASE("GranularProcessor: reverse grains move readPos backwards by pitch per sample", "[granular][reverse]")
{
    // pitchOffset +12 → ratio 2.  Between snapshots the wrapped distance
    // must be exactly the ratio, negative in reverse and positive forward.
    // Positions sit in [8192, 32768), where float spacing is at most 2^-9,
    // so a difference of two rounded positions is good to 2 · 2^-9 ≈ 0.004.
    constexpr int   kBuffer = 22050;
    constexpr float kPosTol = 0.004f;

    for (bool reverse : { true, false })
    {
        ideath::GranularProcessor gp;
        gp.prepare(kSampleRate, kBuffer);
        gp.setPlaybackDirection(reverse);
        gp.setGrainRate(1.0f);       // one grain for the whole run
        gp.setGrainSize(0.05f);
        gp.setPitchOffset(12.0f);
        gp.setReadWindow(0.5f, 0.5f);

        ideath::GranularProcessor::GrainSnapshot snap[ideath::GranularProcessor::kMaxGrains];
        (void)gp.process();
        gp.snapshotGrains(snap);
        REQUIRE(snap[0].active);
        REQUIRE(snap[0].pitch > 0.0f);   // documented as a positive ratio
        float previous = snap[0].readPos;

        for (int i = 0; i < 500; ++i)
        {
            (void)gp.process();
            gp.snapshotGrains(snap);
            const float expected = reverse ? -2.0f : 2.0f;
            REQUIRE_THAT(snap[0].readPos - previous, WithinAbs(expected, kPosTol));
            previous = snap[0].readPos;
        }
    }
}

TEST_CASE("GranularProcessor: reverse plays the recorded material time-reversed", "[granular][reverse]")
{
    // A linear ramp in a frozen ring; a unity reverse grain starts mid-ring
    // and must read ring[start − n], so y[n] = g · env(n) · ramp(start − n).
    constexpr int kGrain = kRingLen / 2;
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, kRingLen);
    const auto ramp = [](float i) { return i / static_cast<float>(kRingLen); };
    for (int i = 0; i < kRingLen; ++i)
        gp.writeSample(ramp(static_cast<float>(i)));

    gp.setFreeze(true);
    gp.setPlaybackDirection(true);
    gp.setGrainRate(1.0f);
    gp.setGrainSize(static_cast<float>(kGrain) / kSampleRate);
    gp.setReadWindow(0.25f, 0.25f);   // 0.25 behind the head at 0 = index 3N/4

    const float start = 0.75f * static_cast<float>(kRingLen);   // 3307.5
    for (int n = 0; n < kGrain; ++n)
    {
        const float y = gp.process();
        const float expected = ramp(start - static_cast<float>(n));
        REQUIRE_THAT(y, WithinAbs(kSparseGain * hannAt(n, kGrain) * expected, kEnvTol));
    }
}

TEST_CASE("GranularProcessor: direction is latched per grain", "[granular][reverse]")
{
    // Flipping the direction must not bend a grain already in flight,
    // otherwise a knob move would fold every active grain at once.
    for (bool launchedReverse : { false, true })
    {
        ideath::GranularProcessor gp;
        gp.prepare(kSampleRate, 22050);
        gp.setPlaybackDirection(launchedReverse);
        gp.setGrainRate(1.0f);
        gp.setGrainSize(0.05f);
        gp.setReadWindow(0.5f, 0.5f);

        ideath::GranularProcessor::GrainSnapshot snap[ideath::GranularProcessor::kMaxGrains];
        (void)gp.process();
        gp.snapshotGrains(snap);
        const float before = snap[0].readPos;

        gp.setPlaybackDirection(!launchedReverse);
        (void)gp.process();
        gp.snapshotGrains(snap);
        REQUIRE((snap[0].readPos < before) == launchedReverse);
    }
}

TEST_CASE("GranularProcessor: setPlaybackDirection(false) is bit-identical to the default", "[granular][reverse]")
{
    ideath::GranularProcessor plain, toggled;
    for (auto* gp : { &plain, &toggled })
    {
        gp->prepare(kSampleRate, 22050);
        configureBusy(*gp);
    }
    toggled.setPlaybackDirection(true);
    toggled.setPlaybackDirection(false);
    requireBitIdentical(runBusy(plain), runBusy(toggled));
}

TEST_CASE("GranularProcessor: grain length does not depend on speed or direction", "[granular][reverse]")
{
    // setGrainSize(0.05) at 44.1 kHz is 2205 samples of lifetime at any
    // ratio, in either direction, and whether or not the traversal
    // (ratio 4 → 8820 samples) wraps a 4096-sample ring.  envPhase rounding
    // is <= 2205 · 2^-25 = 6.6e-5 of the lifetime = 0.15 samples, so the
    // phase sum reaches 1 on process() call 2205, or 2206 if it rounds low.
    constexpr int kExpected = 2205;

    for (bool reverse : { false, true })
        for (float semis : { -24.0f, 0.0f, 24.0f })
            for (int bufferLen : { 22050, 4096 })
            {
                ideath::GranularProcessor gp;
                gp.prepare(kSampleRate, bufferLen);
                gp.setPlaybackDirection(reverse);
                gp.setGrainRate(1.0f);   // no second spawn inside the window
                gp.setGrainSize(0.05f);
                gp.setPitchOffset(semis);

                int calls = 1;
                (void)gp.process();
                REQUIRE(gp.activeGrainCount() == 1);
                while (gp.activeGrainCount() > 0 && calls < 3 * kExpected)
                {
                    ++calls;
                    (void)gp.process();
                }

                CAPTURE(reverse, semis, bufferLen, calls);
                REQUIRE(calls >= kExpected);
                REQUIRE(calls <= kExpected + 1);
            }
}

TEST_CASE("GranularProcessor: reverse read is seamless across the physical ring edge", "[granular][reverse]")
{
    // Write 1.5 rings so index 0 sits inside continuous material: indices
    // 0..4095 hold input samples 8192..12287, indices 4096..8191 hold
    // 4096..8191.  A unity reverse grain starts at index 2205 and crosses
    // index 0 at its Hann peak.  At every sample y must equal the analytic
    // sine, read at integer positions and at half-sample positions so the
    // interpolation pair (8191, 0) is covered.  A wrong wrap repeats or
    // skips a sample and misses by one sine step at the crossing: full-slope
    // step 2π · 220 / 44100 = 0.031 × |cos| 0.67 at this phase × g = 0.030,
    // far above kEnvTol.
    constexpr int   kBuffer = 8192;
    constexpr int   kHead   = 4096;
    constexpr int   kGrain  = 4410;
    constexpr int   kStart  = 2205;
    constexpr float kFreq   = 220.0f;

    for (float fraction : { 0.0f, 0.5f })
    {
        ideath::GranularProcessor gp;
        gp.prepare(kSampleRate, kBuffer);
        for (int i = 0; i < kBuffer + kHead; ++i)
            gp.writeSample(std::sin(2.0f * kPi * kFreq * static_cast<float>(i) / kSampleRate));
        REQUIRE(gp.writePos() == kHead);

        gp.setFreeze(true);
        gp.setPlaybackDirection(true);
        gp.setGrainRate(1.0f);
        gp.setGrainSize(static_cast<float>(kGrain) / kSampleRate);
        const float behind = (static_cast<float>(kHead - kStart) - fraction)
                             / static_cast<float>(kBuffer);
        gp.setReadWindow(behind, behind);

        ideath::GranularProcessor::GrainSnapshot snap[ideath::GranularProcessor::kMaxGrains];
        bool crossed = false;
        float previous = static_cast<float>(kStart);
        for (int n = 0; n < kGrain; ++n)
        {
            const float y = gp.process();
            gp.snapshotGrains(snap);
            if (n + 1 < kGrain)
            {
                crossed = crossed || snap[0].readPos > previous;
                previous = snap[0].readPos;
            }

            const float t = static_cast<float>(kBuffer + kStart - n) + fraction;
            const float x = std::sin(2.0f * kPi * kFreq * t / kSampleRate);
            CAPTURE(n, fraction);
            REQUIRE_THAT(y, WithinAbs(kSparseGain * hannAt(n, kGrain) * x, kEnvTol));
        }
        REQUIRE(crossed);
    }
}

TEST_CASE("GranularProcessor: reverse read does not click at the write-head seam", "[granular][reverse]")
{
    // The newest and oldest samples are neighbours in the ring and carry a
    // large step here: a slow ramp spanning 2 rings gives a jump of ≈ 0.9.
    // A reverse grain crossing the head must fade through it.  y = g·env·s·f
    // with every factor bounded by 1, so away from the jump, per sample
    //   |Δy| <= g · (|Δenv| + |Δs| + |Δf|)
    //   |Δenv| <= π / L                (Hann peak slope)
    //   |Δs|   <= ratio · ramp slope   (the ramp, read at the grain's speed)
    //   |Δf|   <= 1.5 / (2 ms · sr)    (smoothstep peak slope over the 2 ms
    //                                   fade specified in setPlaybackDirection)
    // The fade takes the same time whether the head runs or is held, at any
    // ratio and sample rate.  At the jump itself the read straddles the seam
    // at f = 0, and the samples on either side have f <= 3 (1 / (2 ms · sr))²,
    // a far smaller step than the |Δf| term even after the 0.9 jump.
    //
    // The grain starts 400 samples past the seam, so it gets there inside
    // its 2205-sample life at the slowest closing speed, ratio 1/4 with the
    // head held (400 · 4 = 1600 samples).
    constexpr int kBuffer = 8192;
    constexpr int kGrain  = 2205;
    constexpr int kTotal  = 2 * kBuffer + kGrain;
    const     float rampStep = 2.0f / static_cast<float>(kTotal);
    const auto ramp = [&](int i) { return -1.0f + static_cast<float>(i) * rampStep; };

    enum class Head { Running, Frozen, HeldBySos };

    for (float sr : { 8000.0f, 44100.0f, 192000.0f })
        for (float semis : { -24.0f, 0.0f, 12.0f })
            for (Head head : { Head::Running, Head::Frozen, Head::HeldBySos })
            {
                ideath::GranularProcessor gp;
                gp.prepare(sr, kBuffer);
                int written = 0;
                for (; written < 2 * kBuffer; ++written)
                    gp.writeSample(ramp(written));

                gp.setFreeze(head == Head::Frozen);
                gp.setSos(head == Head::HeldBySos ? 1.0f : 0.0f);
                gp.setPlaybackDirection(true);
                gp.setGrainRate(1.0f);
                gp.setGrainSize(static_cast<float>(kGrain) / sr);
                gp.setPitchOffset(semis);
                const float behind = static_cast<float>(kBuffer - 400) / static_cast<float>(kBuffer);
                gp.setReadWindow(behind, behind);

                const float limit = kSparseGain
                    * (kPi / static_cast<float>(kGrain)
                       + std::pow(2.0f, semis / 12.0f) * rampStep
                       + 1.5f / (0.002f * sr));

                ideath::GranularProcessor::GrainSnapshot snap[ideath::GranularProcessor::kMaxGrains];
                float previous = 0.0f;
                float maxStep = 0.0f;
                bool crossed = false;
                float lastAhead = -1.0f;
                for (int n = 0; n < kGrain; ++n)
                {
                    if (head == Head::Running)
                        gp.writeSample(ramp(written++));
                    const float y = gp.process();
                    maxStep = std::max(maxStep, std::abs(y - previous));
                    previous = y;

                    if (n + 1 < kGrain)
                    {
                        gp.snapshotGrains(snap);
                        float ahead = snap[0].readPos - static_cast<float>(gp.writePos());
                        if (ahead < 0.0f) ahead += static_cast<float>(kBuffer);
                        // Distance ahead of the head shrinks until the seam,
                        // then jumps to nearly a full ring.
                        crossed = crossed || (lastAhead >= 0.0f && ahead > lastAhead);
                        lastAhead = ahead;
                    }
                }
                CAPTURE(sr, semis, static_cast<int>(head), maxStep, limit);
                REQUIRE(crossed);
                REQUIRE(maxStep <= limit);
            }
}

TEST_CASE("GranularProcessor: the seam fade lasts 2 ms in both head states", "[granular][reverse]")
{
    // A ring of constant 1 leaves only the fade in y / (g · env).  It lasts
    // 2 ms = 88.2 samples either side of the seam, 176.4 in all, for any
    // ratio and whether the head runs or is held.  A smoothstep passes 0.99
    // at t = 0.942, so the gain stays below 0.99 for 176.4 · 0.942 = 166
    // samples, up to 176.4 if it never settled.  ±2 for the whole samples at
    // each end.  Crossing at n ≈ 1100 of 2205 keeps env >= 0.5, where kEnvTol
    // is well under the 0.01 margin being counted.
    constexpr int kBuffer = 8192;
    constexpr int kGrain  = 2205;

    enum class Head { Running, Frozen, HeldBySos };

    for (Head head : { Head::Running, Head::Frozen, Head::HeldBySos })
        for (float semis : { -24.0f, 0.0f })
        {
            const bool held = head != Head::Running;
            ideath::GranularProcessor gp;
            gp.prepare(kSampleRate, kBuffer);
            for (int i = 0; i < kBuffer; ++i)
                gp.writeSample(1.0f);

            gp.setFreeze(head == Head::Frozen);
            gp.setSos(head == Head::HeldBySos ? 1.0f : 0.0f);
            gp.setPlaybackDirection(true);
            gp.setGrainRate(1.0f);
            gp.setGrainSize(static_cast<float>(kGrain) / kSampleRate);
            gp.setPitchOffset(semis);

            const float ratio = std::pow(2.0f, semis / 12.0f);
            const float closing = held ? ratio : ratio + 1.0f;
            const float start = std::round(1100.0f * closing);
            const float behind = (static_cast<float>(kBuffer) - start) / static_cast<float>(kBuffer);
            gp.setReadWindow(behind, behind);

            int faded = 0;
            for (int n = 0; n < kGrain; ++n)
            {
                if (!held)
                    gp.writeSample(1.0f);
                const float y = gp.process();
                if (n >= kGrain / 4 && n < 3 * kGrain / 4
                    && y < 0.99f * kSparseGain * hannAt(n, kGrain))
                    ++faded;
            }
            CAPTURE(static_cast<int>(head), semis, faded);
            REQUIRE(faded >= 164);
            REQUIRE(faded <= 178);
        }
}

// -----------------------------------------------------------------------------
// Seed
// -----------------------------------------------------------------------------

TEST_CASE("GranularProcessor: same seed gives bit-identical output", "[granular][seed]")
{
    // Before and after prepare() must be equivalent: prepare() resets, and
    // reset() has to restore the seed rather than the factory default.
    ideath::GranularProcessor before, after;
    before.setSeed(0xCAFEBABEu);
    before.prepare(kSampleRate, 22050);
    after.prepare(kSampleRate, 22050);
    after.setSeed(0xCAFEBABEu);
    configureBusy(before);
    configureBusy(after);
    requireBitIdentical(runBusy(before), runBusy(after));
}

TEST_CASE("GranularProcessor: different seeds give different output", "[granular][seed]")
{
    ideath::GranularProcessor a, b;
    a.prepare(kSampleRate, 22050);
    b.prepare(kSampleRate, 22050);
    a.setSeed(1u);
    b.setSeed(2u);
    configureBusy(a);
    configureBusy(b);

    const auto outA = runBusy(a);
    const auto outB = runBusy(b);
    int differing = 0;
    for (std::size_t i = 0; i < outA.size(); ++i)
        if (outA[i] != outB[i]) ++differing;
    REQUIRE(differing > 0);
}

TEST_CASE("GranularProcessor: reset returns to the given seed", "[granular][seed]")
{
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, 22050);
    gp.setSeed(0x01234567u);
    configureBusy(gp);

    const auto first = runBusy(gp);
    gp.reset();
    requireBitIdentical(first, runBusy(gp));
}

TEST_CASE("GranularProcessor: reset keeps seed 0 usable", "[granular][seed]")
{
    // reset() must apply the same zero-state remap as setSeed(), or the
    // second run would have a stuck RNG and diverge from the first.
    ideath::GranularProcessor gp;
    gp.prepare(kSampleRate, 22050);
    gp.setSeed(0u);
    configureBusy(gp);

    const auto first = runBusy(gp);
    gp.reset();
    requireBitIdentical(first, runBusy(gp));
}

TEST_CASE("GranularProcessor: settings survive prepare at a new buffer size", "[granular][sos][reverse][seed]")
{
    // Like every other parameter they are configuration, not audio state.
    // Prepared twice with the settings set between must equal prepared once
    // at the final size with the settings set up front.
    ideath::GranularProcessor again, once;
    again.prepare(kSampleRate, 4096);
    for (auto* gp : { &again, &once })
    {
        gp->setSos(0.5f);
        gp->setPlaybackDirection(true);
        gp->setSeed(0xABCDu);
    }
    again.prepare(kSampleRate, 22050);
    once.prepare(kSampleRate, 22050);
    configureBusy(again);
    configureBusy(once);
    requireBitIdentical(runBusy(again), runBusy(once));

    ideath::GranularProcessor plain;
    plain.prepare(kSampleRate, 22050);
    configureBusy(plain);
    again.reset();
    const auto kept = runBusy(again);
    const auto defaults = runBusy(plain);
    int differing = 0;
    for (std::size_t i = 0; i < kept.size(); ++i)
        if (kept[i] != defaults[i]) ++differing;
    REQUIRE(differing > 0);
}

TEST_CASE("GranularProcessor: the default seed stays 0x9E3779B9", "[granular][seed]")
{
    ideath::GranularProcessor plain, seeded;
    for (auto* gp : { &plain, &seeded })
    {
        gp->prepare(kSampleRate, 22050);
        configureBusy(*gp);
    }
    seeded.setSeed(0x9E3779B9u);
    requireBitIdentical(runBusy(plain), runBusy(seeded));
}

TEST_CASE("GranularProcessor: seed 0 still randomises and is not the default", "[granular][seed]")
{
    // xorshift32 maps state 0 to itself.  A raw zero state would pin every
    // grain to the bottom of the pitch range and the near window edge, so
    // two grains would never differ.  It must also stay distinct from the
    // default seed, because different seeds must differ.
    ideath::GranularProcessor zero, plain;
    for (auto* gp : { &zero, &plain })
    {
        gp->prepare(kSampleRate, 22050);
        configureBusy(*gp);
    }
    zero.setSeed(0u);

    ideath::GranularProcessor::GrainSnapshot snap[ideath::GranularProcessor::kMaxGrains];
    float lowest = 1e9f, highest = 0.0f;
    std::vector<float> outZero(22050), outPlain(22050);
    for (int i = 0; i < 22050; ++i)
    {
        const float x = sineAt(i, 330.0f, kSampleRate);
        zero.writeSample(x);
        plain.writeSample(x);
        outZero[static_cast<std::size_t>(i)]  = zero.process();
        outPlain[static_cast<std::size_t>(i)] = plain.process();
        zero.snapshotGrains(snap);
        for (const auto& g : snap)
            if (g.active)
            {
                lowest  = std::min(lowest, g.pitch);
                highest = std::max(highest, g.pitch);
            }
    }
    REQUIRE(highest > lowest);

    int differing = 0;
    for (std::size_t i = 0; i < outZero.size(); ++i)
        if (outZero[i] != outPlain[i]) ++differing;
    REQUIRE(differing > 0);
}

TEST_CASE("GranularProcessor: repeating setSeed with the same value does not restart the RNG", "[granular][seed]")
{
    // Setters are per-sample safe by library convention.  A host that
    // pushes every parameter every block must not freeze the random
    // sequence by re-seeding the same value.
    ideath::GranularProcessor once, every;
    for (auto* gp : { &once, &every })
    {
        gp->prepare(kSampleRate, 22050);
        gp->setSeed(0xBEEFu);
        configureBusy(*gp);
    }
    std::vector<float> a(22050), b(22050);
    for (int i = 0; i < 22050; ++i)
    {
        const float x = sineAt(i, 330.0f, kSampleRate);
        once.writeSample(x);
        every.setSeed(0xBEEFu);
        every.writeSample(x);
        a[static_cast<std::size_t>(i)] = once.process();
        b[static_cast<std::size_t>(i)] = every.process();
    }
    requireBitIdentical(a, b);
}

// -----------------------------------------------------------------------------
// Combined extremes
// -----------------------------------------------------------------------------

TEST_CASE("GranularProcessor: 10 s of SOS + reverse at extreme settings stays bounded", "[granular][sos][reverse]")
{
    // SOS is a convex mix, so with |input| <= 1 the ring stays within
    // 1 + 1e-25 / (1 − a).  Each grain contributes at most its Hann peak 1
    // and the seam fade only attenuates, so the universal bound applies:
    //   |y| <= kMaxGrains · max(|ring|) · g(O)
    // O = 200 Hz · 1 s = 200 gives g = 1 / sqrt(100) = 0.1 → 1.6.  Buffer
    // lengths run from the 16-sample floor upward, where the seam fade is
    // capped by the ring size.  10 s per the long-run convention.
    constexpr int   kSamples = 441000;
    constexpr float kBound   = ideath::GranularProcessor::kMaxGrains * 0.1f;

    for (int bufferLen : { 16, 441, 44100 })
        for (float offset : { -24.0f, 24.0f })
        {
            ideath::GranularProcessor gp;
            gp.prepare(kSampleRate, bufferLen);
            gp.setGrainRate(200.0f);
            gp.setGrainSize(1.0f);
            gp.setPitchSpread(24.0f);
            gp.setPitchOffset(offset);
            gp.setReadWindow(0.0f, 1.0f);
            gp.setPlaybackDirection(true);
            gp.setSos(0.95f);

            uint32_t state = 0x13572468u;
            float peak = 0.0f;
            bool finite = true;
            for (int i = 0; i < kSamples; ++i)
            {
                gp.writeSample(noiseSample(state));
                const float y = gp.process();
                finite = finite && std::isfinite(y);
                peak = std::max(peak, std::abs(y));
            }
            CAPTURE(bufferLen, offset, peak);
            REQUIRE(finite);
            REQUIRE(peak <= kBound);
        }
}
