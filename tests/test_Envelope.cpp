#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Envelope.h>
#include <cmath>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

// Threshold derivations used throughout this file
// -----------------------------------------------
// All envelope coefficients in Envelope.cpp follow the same -60 dB convention:
//     coef = exp(-6.9078 / (timeSeconds * sampleRate))
// where 6.9078 ≈ ln(1000), so level × coef^N reaches 1e-3 (-60 dB) in exactly
// (timeSeconds * sampleRate) samples. The flush-to-zero threshold is 1e-5
// (DecayEnvelope.kSilenceThreshold, ADSR release 1e-5, AR release 1e-5), so
// "10× the configured time" is sufficient for any test that needs fully
// decayed output — by then level has dropped well below the flush threshold
// and been snapped to 0.
//
// Retrigger fade coefficient is hard-coded in prepare():
//     retriggerSamples = 0.001 * sampleRate = 44.1 @ 44.1 kHz
//     retriggerCoef = exp(-6.9078 / 44.1) ≈ 0.8549
// Retrigger exit threshold is 0.001 (AdsrEnvelope::process). From starting
// level L the fade completes in ceil(ln(0.001/L) / ln(0.8549)) samples.

// ---- DecayEnvelope ----

TEST_CASE("DecayEnvelope: starts inactive", "[env]")
{
    ideath::DecayEnvelope env;
    env.prepare(kSampleRate);
    REQUIRE_FALSE(env.isActive());
    // Inactive branch returns 0.0f literal — tolerance is for floating-point
    // equality only, there is no arithmetic noise.
    REQUIRE_THAT(env.process(), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("DecayEnvelope: trigger sets level and activates", "[env]")
{
    ideath::DecayEnvelope env;
    env.prepare(kSampleRate);
    env.setDecay(0.1f);
    env.trigger(1.0f);

    REQUIRE(env.isActive());
    // process() returns `out = level_` *before* the decay multiply, and
    // trigger() wrote level_ = 1.0 exactly. So the first sample is 1.0
    // bit-for-bit; 1e-6 is a generous numeric tolerance.
    float first = env.process();
    REQUIRE_THAT(first, WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("DecayEnvelope: decays to silence", "[env]")
{
    ideath::DecayEnvelope env;
    env.prepare(kSampleRate);
    env.setDecay(0.01f); // very short — 441 samples to -60 dB

    env.trigger(1.0f);

    // Process 100 ms = 4410 samples = 10 × decay time. Level reaches the
    // 1e-5 flush-to-zero threshold at N = ln(1e-5)/ln(exp(-6.9078/441))
    // ≈ 735 samples, well before 4410. After flush, process() returns 0
    // exactly and active_ = false.
    float last = 0.0f;
    for (int i = 0; i < 4410; ++i)
        last = env.process();

    REQUIRE_THAT(last, WithinAbs(0.0f, 1e-6f));
    REQUIRE_FALSE(env.isActive());
}

TEST_CASE("DecayEnvelope: monotonically decreasing", "[env]")
{
    ideath::DecayEnvelope env;
    env.prepare(kSampleRate);
    env.setDecay(0.05f);
    env.trigger(1.0f);

    // With decay = 50 ms, coef = exp(-6.9078/2205) ≈ 0.99687. Each sample
    // multiplies level_ by coef < 1, so the sequence is strictly decreasing
    // in real arithmetic. Float rounding can occasionally produce a step
    // equal to the previous value when level × (1 − coef) falls below the
    // local ULP; 1e-7 covers that without masking any real violation.
    // 2000 samples stays above the 1e-5 flush threshold (flushes at
    // ~3680 samples from level 1.0), so the test exercises pure
    // multiplicative decay.
    float prev = env.process();
    for (int i = 1; i < 2000; ++i)
    {
        float cur = env.process();
        REQUIRE(cur <= prev + 1e-7f);
        prev = cur;
    }
}

TEST_CASE("DecayEnvelope: reset clears state", "[env]")
{
    ideath::DecayEnvelope env;
    env.prepare(kSampleRate);
    env.setDecay(0.1f);
    env.trigger(1.0f);
    for (int i = 0; i < 100; ++i)
        env.process();

    env.reset();
    REQUIRE_FALSE(env.isActive());
    // reset() assigns level_ = 0.0f literal — tolerance is for equality only.
    REQUIRE_THAT(env.getValue(), WithinAbs(0.0f, 1e-6f));
}

// ---- AdsrEnvelope ----

TEST_CASE("ADSR: starts idle", "[env][adsr]")
{
    ideath::AdsrEnvelope env;
    env.prepare(kSampleRate);
    REQUIRE(env.getStage() == ideath::AdsrEnvelope::Stage::Idle);
    // Idle branch assigns level_ = 0.0f then returns it — exact zero.
    REQUIRE_THAT(env.process(), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("ADSR: attack rises to 1.0", "[env][adsr]")
{
    ideath::AdsrEnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.01f);    // 10 ms = 441 samples
    env.setDecay(0.05f);
    env.setSustain(0.5f);
    env.setRelease(0.1f);
    env.noteOn();

    // Attack is a linear ramp: level_ += 1/441 per sample. When level_
    // reaches 1.0, it's clamped to 1.0 exactly and stage transitions to
    // Decay within the same process() call. The test runs 100 ms so the
    // ramp finishes well before the loop ends. Peak must equal 1.0 bit-
    // for-bit; 1e-5 is numeric tolerance on the clamp.
    float peak = 0.0f;
    for (int i = 0; i < 4410; ++i)
    {
        float v = env.process();
        if (v > peak) peak = v;
    }

    REQUIRE_THAT(peak, WithinAbs(1.0f, 1e-5f));
}

TEST_CASE("ADSR: sustain holds at set level", "[env][adsr]")
{
    ideath::AdsrEnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.001f);   // 44 samples
    env.setDecay(0.01f);     // 441 samples → coef = exp(-6.9078/441) ≈ 0.9844
    env.setSustain(0.6f);
    env.setRelease(0.1f);
    env.noteOn();

    // Attack + decay finishes quickly:
    //   attack  = 44 samples
    //   decay converges when diff = (1 − 0.6) × coef^N < 1e-4, i.e.
    //   N = ln(1e-4/0.4) / ln(0.9844) ≈ 520 samples.
    // 4410 samples ≫ 44 + 520, so we are firmly in Sustain.
    for (int i = 0; i < 4410; ++i)
        env.process();

    // In Sustain, level_ is assigned sustainLevel_ each call — the only
    // error is the float representation of 0.6f itself (1 ULP ≈ 6e-8).
    REQUIRE(env.getStage() == ideath::AdsrEnvelope::Stage::Sustain);
    float v = env.process();
    REQUIRE_THAT(v, WithinAbs(0.6f, 1e-6f));
}

TEST_CASE("ADSR: release decays to zero", "[env][adsr]")
{
    ideath::AdsrEnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.001f);
    env.setDecay(0.001f);
    env.setSustain(0.5f);
    env.setRelease(0.01f); // 441 samples to -60 dB

    env.noteOn();
    for (int i = 0; i < 2000; ++i)
        env.process();

    env.noteOff();

    // 100 ms = 10× release time. Release flushes to 0 at the 1e-5 threshold,
    // which from sustain 0.5 happens at N = ln(1e-5/0.5) / ln(0.9844) ≈ 690
    // samples — far inside the 4410-sample window. level_ = 0 exactly and
    // stage → Idle.
    float last = 0.0f;
    for (int i = 0; i < 4410; ++i)
        last = env.process();

    REQUIRE_THAT(last, WithinAbs(0.0f, 1e-6f));
    REQUIRE(env.getStage() == ideath::AdsrEnvelope::Stage::Idle);
}

TEST_CASE("ADSR: reset returns to idle", "[env][adsr]")
{
    ideath::AdsrEnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.01f);
    env.noteOn();
    for (int i = 0; i < 500; ++i)
        env.process();

    env.reset();
    REQUIRE(env.getStage() == ideath::AdsrEnvelope::Stage::Idle);
    // reset() assigns level_ = 0.0f literal.
    REQUIRE_THAT(env.getValue(), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("ADSR: retrigger during release fades before attack", "[env][adsr]")
{
    ideath::AdsrEnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.005f);
    env.setDecay(0.05f);
    env.setSustain(0.5f);
    env.setRelease(0.1f);   // coef = exp(-6.9078/4410) ≈ 0.99844

    env.noteOn();
    // Advance into sustain (220 attack + ~2720 decay < 4410).
    for (int i = 0; i < 4410; ++i)
        env.process();
    REQUIRE(env.getStage() == ideath::AdsrEnvelope::Stage::Sustain);

    // 200 samples of release from sustain = 0.5:
    //   level = 0.5 × 0.99844^200 ≈ 0.5 × 0.7306 ≈ 0.365
    env.noteOff();
    for (int i = 0; i < 200; ++i)
        env.process();
    float levelBeforeRetrigger = env.getValue();
    REQUIRE_THAT(levelBeforeRetrigger, WithinAbs(0.365f, 0.01f));

    // Retrigger fade from 0.365 down to the 0.001 exit threshold:
    //   N = ln(0.001 / 0.365) / ln(0.8549) ≈ 38 samples.
    env.noteOn();
    REQUIRE(env.getStage() == ideath::AdsrEnvelope::Stage::Retrigger);

    float prev = levelBeforeRetrigger;
    bool monotonic = true;
    int samplesToAttack = 0;
    for (int i = 0; i < 200; ++i)
    {
        float v = env.process();
        // Retrigger is a pure geometric decay (× coef < 1), so strictly
        // non-increasing apart from float rounding (< 1 ULP).
        if (v > prev + 1e-7f) monotonic = false;
        prev = v;
        if (env.getStage() == ideath::AdsrEnvelope::Stage::Attack)
        {
            samplesToAttack = i;
            break;
        }
    }

    REQUIRE(monotonic);
    // Derived bound: ~38 samples; allow a ±50% window to cover the exact
    // float trajectory without admitting the pre-fix "jump to Attack"
    // regression (which would give samplesToAttack == 0).
    REQUIRE(samplesToAttack > 20);
    REQUIRE(samplesToAttack < 60);
    // Exit threshold for Retrigger is level_ < 0.001f.
    REQUIRE(env.getValue() < 0.001f);
}

TEST_CASE("ADSR: retrigger from idle skips fade", "[env][adsr]")
{
    ideath::AdsrEnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.01f);
    env.setDecay(0.05f);
    env.setSustain(0.5f);
    env.setRelease(0.1f);

    // From Idle (level_ == 0 ≤ 0.001), noteOn takes the Attack branch
    // directly — no retrigger fade needed because there's nothing to fade.
    env.noteOn();
    REQUIRE(env.getStage() == ideath::AdsrEnvelope::Stage::Attack);
}

// ---- AREnvelope ----

TEST_CASE("AR: starts idle and outputs zero", "[env][ar]")
{
    ideath::AREnvelope env;
    env.prepare(kSampleRate);
    REQUIRE(env.getStage() == ideath::AREnvelope::Stage::Idle);
    REQUIRE_FALSE(env.isActive());
    // Idle branch assigns level_ = 0.0f literal.
    REQUIRE_THAT(env.process(), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("AR: attack rises to 1.0 then enters sustain", "[env][ar]")
{
    ideath::AREnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.01f);   // 441 samples, attackRate = 1/441
    env.setRelease(0.05f);
    env.noteOn();

    // Same shape as ADSR attack: linear ramp clamped to 1.0 on crossing.
    // In 100 ms (4410 samples) the ramp completes and sustain holds at
    // 1.0 exactly.
    float peak = 0.0f;
    for (int i = 0; i < 4410; ++i)
    {
        float v = env.process();
        if (v > peak) peak = v;
    }

    REQUIRE_THAT(peak, WithinAbs(1.0f, 1e-5f));
    REQUIRE(env.getStage() == ideath::AREnvelope::Stage::Sustain);
}

TEST_CASE("AR: sustain holds at 1.0", "[env][ar]")
{
    ideath::AREnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.001f);
    env.setRelease(0.1f);
    env.noteOn();

    for (int i = 0; i < 4410; ++i)
        env.process();

    // Sustain branch: level_ = 1.0f literal per call → bit-exact.
    REQUIRE(env.getStage() == ideath::AREnvelope::Stage::Sustain);
    for (int i = 0; i < 1000; ++i)
        REQUIRE_THAT(env.process(), WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("AR: release decays to zero and returns to idle", "[env][ar]")
{
    ideath::AREnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.001f);
    env.setRelease(0.01f);  // 441 samples to -60 dB

    env.noteOn();
    for (int i = 0; i < 2000; ++i)
        env.process();
    REQUIRE(env.getStage() == ideath::AREnvelope::Stage::Sustain);

    env.noteOff();
    REQUIRE(env.getStage() == ideath::AREnvelope::Stage::Release);

    // 4410 samples = 10× release. Flush-to-zero at 1e-5 from level 1.0
    // hits at ~735 samples; stage transitions to Idle and level = 0.
    float last = 0.0f;
    for (int i = 0; i < 4410; ++i)
        last = env.process();

    REQUIRE_THAT(last, WithinAbs(0.0f, 1e-6f));
    REQUIRE(env.getStage() == ideath::AREnvelope::Stage::Idle);
    REQUIRE_FALSE(env.isActive());
}

TEST_CASE("AR: isActive tracks stage transitions", "[env][ar]")
{
    ideath::AREnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.005f);
    env.setRelease(0.005f);

    REQUIRE_FALSE(env.isActive());
    env.noteOn();
    REQUIRE(env.isActive());

    for (int i = 0; i < 1000; ++i)
        env.process();
    REQUIRE(env.isActive());

    env.noteOff();
    REQUIRE(env.isActive());

    // 4410 samples ≫ 10× release time (2205 samples to -60 dB) — fully
    // decayed, back to Idle.
    for (int i = 0; i < 4410; ++i)
        env.process();
    REQUIRE_FALSE(env.isActive());
}

TEST_CASE("AR: reset returns to idle", "[env][ar]")
{
    ideath::AREnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.05f);
    env.setRelease(0.05f);
    env.noteOn();
    for (int i = 0; i < 500; ++i)
        env.process();

    env.reset();
    REQUIRE(env.getStage() == ideath::AREnvelope::Stage::Idle);
    REQUIRE_THAT(env.getValue(), WithinAbs(0.0f, 1e-6f));
}

// ---------------------------------------------------------------------------
// AdsrEnvelope curve shaping
// ---------------------------------------------------------------------------

namespace {

// Run an ADSR with the given curve and capture the level at sample i=2200
// of the attack segment.
//
// Attack is 100 ms = 4410 samples, attackRate = 1/4410. After 2200 process()
// calls, internal level_ = 2200/4410 ≈ 0.49887 (linear ramp). The curve
// shaper returns pow(level_, curveExponent), where curveExponent = 2^curve:
//   curve=0   → exponent=1.0  → 0.49887
//   curve=-1  → exponent=0.5  → sqrt(0.49887)  ≈ 0.70631
//   curve=+1  → exponent=2.0  → 0.49887^2      ≈ 0.24887
float midAttackLevel(float curve)
{
    ideath::AdsrEnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.1f);    // 100 ms — 4410 samples
    env.setDecay(0.5f);
    env.setSustain(0.5f);
    env.setRelease(0.1f);
    env.setCurve(curve);
    env.noteOn();

    float v = 0.0f;
    for (int i = 0; i < 2200; ++i)
        v = env.process();
    return v;
}

} // namespace

TEST_CASE("ADSR: Curve=0 preserves linear attack midpoint", "[env][adsr][adr009]")
{
    // Expected: 2200/4410 = 0.49887. Tolerance 1e-4 accommodates float
    // accumulation over 2200 additions of 1/4410.
    REQUIRE_THAT(midAttackLevel(0.0f), WithinAbs(0.4989f, 1e-4f));
}

TEST_CASE("ADSR: Curve=-1 raises attack midpoint", "[env][adsr][adr009]")
{
    // Logarithmic shape (exponent 0.5) → sqrt(0.49887) ≈ 0.70631.
    // Difference from linear: 0.70631 − 0.49887 ≈ 0.207. Threshold 0.15
    // keeps a safety margin while still forbidding the pre-curve linear
    // behaviour (which would give difference ≈ 0).
    const float linear = midAttackLevel(0.0f);
    const float log = midAttackLevel(-1.0f);
    REQUIRE(log > linear + 0.15f);
}

TEST_CASE("ADSR: Curve=+1 lowers attack midpoint", "[env][adsr][adr009]")
{
    // Exponential shape (exponent 2.0) → 0.49887^2 ≈ 0.24887.
    // Difference: 0.49887 − 0.24887 = 0.25. Threshold 0.15 leaves margin.
    const float linear = midAttackLevel(0.0f);
    const float exp = midAttackLevel(1.0f);
    REQUIRE(exp < linear - 0.15f);
}

TEST_CASE("ADSR: Curve clamps out-of-range input", "[env][adsr][adr009]")
{
    ideath::AdsrEnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.01f);
    env.setDecay(0.05f);
    env.setSustain(0.5f);
    env.setRelease(0.05f);

    env.setCurve(-99.0f);  // clamped to -1 by std::clamp
    env.noteOn();
    for (int i = 0; i < 4410; ++i)
        REQUIRE(std::isfinite(env.process()));

    env.reset();
    env.setCurve(99.0f);   // clamped to +1
    env.noteOn();
    for (int i = 0; i < 4410; ++i)
        REQUIRE(std::isfinite(env.process()));
}

TEST_CASE("ADSR: Curve still reaches peak and zero", "[env][adsr][adr009]")
{
    for (float curve : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
    {
        ideath::AdsrEnvelope env;
        env.prepare(kSampleRate);
        env.setAttack(0.01f);
        env.setDecay(0.001f);
        env.setSustain(0.6f);
        env.setRelease(0.02f);
        env.setCurve(curve);

        env.noteOn();
        float peak = 0.0f;
        for (int i = 0; i < 4410; ++i)
        {
            float v = env.process();
            if (v > peak) peak = v;
        }
        // At the sample where level_ crosses 1.0, the Attack case clamps
        // level_ = 1.0 and transitions to stage_ = Decay before the curve
        // shaper runs. Curve shaping only rewrites the output when
        // stage_ == Attack (or Release/Retrigger), so on the transition
        // sample the fallback `return level_` fires and output = 1.0
        // regardless of curve. Peak must equal 1.0 bit-exact.
        REQUIRE_THAT(peak, WithinAbs(1.0f, 1e-5f));

        env.noteOff();
        // 8820 samples = 441× release time (20 ms) — fully decayed.
        float last = 0.0f;
        for (int i = 0; i < 8820; ++i)
            last = env.process();
        REQUIRE_THAT(last, WithinAbs(0.0f, 1e-6f));
    }
}

TEST_CASE("ADSR: Curve does not introduce a release jump", "[env][adsr][adr009]")
{
    ideath::AdsrEnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.005f);
    env.setDecay(0.005f);
    env.setSustain(0.4f);
    env.setRelease(0.1f);
    env.setCurve(1.0f);

    env.noteOn();
    for (int i = 0; i < 4410; ++i)
        env.process();

    // At noteOff, releaseStartLevel_ = level_ = 0.4. First release sample:
    //   level_ *= exp(-6.9078/4410) ≈ 0.99844
    //   output  = 0.4 × (0.99844)^curveExponent = 0.4 × 0.99844^2
    //           ≈ 0.4 × 0.99688 ≈ 0.39875
    // Previous sustain sample output = 0.4. Delta ≈ 0.00125.
    // Tolerance 0.01 is 8× that — generous but forbids the pre-fix
    // discontinuity (which jumped to ~0.16 at curve=+1, delta ≈ 0.24).
    REQUIRE(env.getStage() == ideath::AdsrEnvelope::Stage::Sustain);
    float prev = env.process();
    env.noteOff();
    float first = env.process();
    REQUIRE_THAT(first, WithinAbs(prev, 0.01f));
}

TEST_CASE("ADSR: retrigger during curved release stays continuous", "[env][adsr][adr009]")
{
    // Regression: curve shaping originally only applied to Attack and
    // Release. During Retrigger the raw `level_` was returned, so a noteOn
    // during a curved release jumped from
    //     releaseStartLevel × pow(level / releaseStartLevel, exponent)
    // back up to bare level. At curve=+1, sustain=0.6 this produced a
    // step of ~0.2.
    //
    // Per-sample delta at curve=+1, release=0.2s, after 800 release samples:
    //   level_before = 0.6 × 0.99922^800 ≈ 0.3207
    //   out_before   = 0.6 × (0.3207/0.6)^2 ≈ 0.1714
    //   level_after  = 0.3207 × 0.8549 (retriggerCoef) ≈ 0.2742
    //   out_after    = 0.6 × (0.2742/0.6)^2 ≈ 0.1253
    //   delta ≈ 0.046
    // For |curve| < 1 the exponent is closer to 1 and delta is smaller.
    // Threshold 0.05 is the computed worst case + ~10% float headroom;
    // the regression would produce delta > 0.2, so the gap is clean.
    for (float curve : { -1.0f, -0.5f, 0.5f, 1.0f })
    {
        ideath::AdsrEnvelope env;
        env.prepare(kSampleRate);
        env.setAttack(0.005f);
        env.setDecay(0.05f);
        env.setSustain(0.6f);
        env.setRelease(0.20f);
        env.setCurve(curve);

        env.noteOn();
        for (int i = 0; i < 4410; ++i)
            env.process();
        REQUIRE(env.getStage() == ideath::AdsrEnvelope::Stage::Sustain);

        env.noteOff();
        for (int i = 0; i < 800; ++i)
            env.process();

        float beforeRetrigger = env.process();
        env.noteOn();
        REQUIRE(env.getStage() == ideath::AdsrEnvelope::Stage::Retrigger);
        float afterRetrigger = env.process();

        INFO("curve=" << curve
             << " before=" << beforeRetrigger
             << " after=" << afterRetrigger);
        REQUIRE_THAT(afterRetrigger, WithinAbs(beforeRetrigger, 0.05f));
    }
}

TEST_CASE("ADSR: retrigger output is continuous (no jumps)", "[env][adsr]")
{
    ideath::AdsrEnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.005f);
    env.setDecay(0.10f);
    env.setSustain(0.3f);
    env.setRelease(0.05f);

    env.noteOn();
    for (int i = 0; i < 4410; ++i)
        env.process();

    env.noteOff();
    for (int i = 0; i < 100; ++i)
        env.process();

    // At the retrigger boundary (no curve, so curveExponent=1, shaper is
    // short-circuited), the first retrigger sample multiplies level_ by
    // retriggerCoef ≈ 0.8549. The largest per-sample delta over the
    // 500-sample window is this first step. Derivation:
    //   Decay has not finished at 4410 samples (decay=100ms ≈ 5674 samples
    //   to converge to sustain), so level at noteOff is slightly above 0.3.
    //   After 100 release samples (coef ≈ 0.99687) level ≈ 0.220.
    //   First retrigger delta ≈ 0.220 × (1 − 0.8549) ≈ 0.032.
    // Threshold 0.04 is the derived peak + 25% float headroom. A true
    // step discontinuity (the bug this test guards) would exceed 0.1.
    float prev = env.process();
    env.noteOn();

    float maxDelta = 0.0f;
    for (int i = 0; i < 500; ++i)
    {
        float v = env.process();
        float delta = std::fabs(v - prev);
        maxDelta = std::max(maxDelta, delta);
        prev = v;
    }

    INFO("Max sample-to-sample delta during retrigger: " << maxDelta);
    REQUIRE(maxDelta < 0.04f);
}

// ---------------------------------------------------------------------------
// Long-run stability and extreme parameter coverage
// ---------------------------------------------------------------------------

TEST_CASE("Envelope: 10-second stability across gate cycles", "[env][stability]")
{
    // ADSR and AR lack recirculating state, but we still run 10 s of
    // repeated gate cycles to catch any pathological flush/re-activation
    // bug (level not returning to zero, stage stuck, etc.).
    ideath::AdsrEnvelope adsr;
    ideath::AREnvelope ar;
    ideath::DecayEnvelope dec;
    adsr.prepare(kSampleRate);
    adsr.setAttack(0.005f); adsr.setDecay(0.02f);
    adsr.setSustain(0.5f);  adsr.setRelease(0.01f);
    ar.prepare(kSampleRate);
    ar.setAttack(0.005f); ar.setRelease(0.01f);
    dec.prepare(kSampleRate);
    dec.setDecay(0.05f);

    constexpr int N = 441000;       // 10 s
    constexpr int kNoteSamples = 4410; // 100 ms on, 100 ms off → 50 gate cycles

    for (int i = 0; i < N; ++i)
    {
        if (i % (2 * kNoteSamples) == 0)
        {
            adsr.noteOn();
            ar.noteOn();
            dec.trigger(1.0f);
        }
        else if (i % (2 * kNoteSamples) == kNoteSamples)
        {
            adsr.noteOff();
            ar.noteOff();
        }
        float a = adsr.process();
        float b = ar.process();
        float c = dec.process();
        REQUIRE(std::isfinite(a));
        REQUIRE(std::isfinite(b));
        // Linear attack / geometric decay are bounded by [0, 1] for ADSR
        // (sustain ≤ 1) and AR (sustain = 1). Decay ≤ trigger level = 1.
        REQUIRE(a >= 0.0f);
        REQUIRE(a <= 1.0f);
        REQUIRE(b >= 0.0f);
        REQUIRE(b <= 1.0f);
        REQUIRE(c >= 0.0f);
        REQUIRE(c <= 1.0f);
        REQUIRE(std::isfinite(c));
    }
}

TEST_CASE("Envelope: extreme parameter combinations stay bounded", "[env][stability]")
{
    // Minimum times: attack = decay = release = 0. setXxx clamps
    // (timeSeconds * sampleRate) to >= 1, giving a 1-sample ramp/decay.
    // Maximum sustain = 1.0 (clamped). This produces the fastest and
    // most numerically stressful transitions — verify finite output.
    {
        ideath::AdsrEnvelope env;
        env.prepare(kSampleRate);
        env.setAttack(0.0f);
        env.setDecay(0.0f);
        env.setSustain(2.0f);    // clamped to 1.0
        env.setRelease(0.0f);
        env.setCurve(1.0f);

        env.noteOn();
        for (int i = 0; i < 44100; ++i)
        {
            float v = env.process();
            REQUIRE(std::isfinite(v));
            REQUIRE(v >= 0.0f);
            REQUIRE(v <= 1.0f);
        }
        env.noteOff();
        for (int i = 0; i < 44100; ++i)
        {
            float v = env.process();
            REQUIRE(std::isfinite(v));
            REQUIRE(v >= 0.0f);
            REQUIRE(v <= 1.0f);
        }
    }

    // Very long times: 10 s attack/release. attackRate ≈ 1/441000,
    // releaseCoef ≈ exp(-6.9078/441000) ≈ 0.99998436. Over 44100 samples
    // (= 1 s) we exercise only ~10% of each segment. Level must stay
    // inside [0, 1] with no overflow/denormal issues.
    {
        ideath::AREnvelope env;
        env.prepare(kSampleRate);
        env.setAttack(10.0f);
        env.setRelease(10.0f);
        env.noteOn();
        for (int i = 0; i < 44100; ++i)
        {
            float v = env.process();
            REQUIRE(std::isfinite(v));
            REQUIRE(v >= 0.0f);
            REQUIRE(v <= 1.0f);
        }
    }

    // Negative inputs should be clamped. setSustain(-1) → 0; setCurve(-99)
    // → -1. Envelope must behave sensibly (non-negative output, bounded).
    {
        ideath::AdsrEnvelope env;
        env.prepare(kSampleRate);
        env.setAttack(0.01f);
        env.setDecay(0.01f);
        env.setSustain(-1.0f);   // clamped to 0
        env.setRelease(0.01f);
        env.setCurve(-99.0f);    // clamped to -1 (exponent 0.5)

        env.noteOn();
        for (int i = 0; i < 4410; ++i)
        {
            float v = env.process();
            REQUIRE(std::isfinite(v));
            REQUIRE(v >= 0.0f);
            // Curve=-1 is sqrt shape: sqrt(x) ≤ 1 for x ∈ [0,1].
            REQUIRE(v <= 1.0f);
        }
    }
}
