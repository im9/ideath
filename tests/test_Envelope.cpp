#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Envelope.h>
#include <cmath>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

// ---- DecayEnvelope ----

TEST_CASE("DecayEnvelope: starts inactive", "[env]")
{
    ideath::DecayEnvelope env;
    env.prepare(kSampleRate);
    REQUIRE_FALSE(env.isActive());
    REQUIRE_THAT(env.process(), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("DecayEnvelope: trigger sets level and activates", "[env]")
{
    ideath::DecayEnvelope env;
    env.prepare(kSampleRate);
    env.setDecay(0.1f);
    env.trigger(1.0f);

    REQUIRE(env.isActive());
    float first = env.process();
    REQUIRE(first > 0.9f);
}

TEST_CASE("DecayEnvelope: decays to silence", "[env]")
{
    ideath::DecayEnvelope env;
    env.prepare(kSampleRate);
    env.setDecay(0.01f); // very short

    env.trigger(1.0f);

    // Process enough samples for a 10ms decay.
    float last = 0.0f;
    for (int i = 0; i < 4410; ++i) // 100ms — way more than 10ms decay
        last = env.process();

    REQUIRE(last < 0.001f);
    REQUIRE_FALSE(env.isActive());
}

TEST_CASE("DecayEnvelope: monotonically decreasing", "[env]")
{
    ideath::DecayEnvelope env;
    env.prepare(kSampleRate);
    env.setDecay(0.05f);
    env.trigger(1.0f);

    float prev = env.process();
    for (int i = 1; i < 2000; ++i)
    {
        float cur = env.process();
        REQUIRE(cur <= prev + 1e-7f); // allow tiny float rounding
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
    REQUIRE_THAT(env.getValue(), WithinAbs(0.0f, 1e-6f));
}

// ---- AdsrEnvelope ----

TEST_CASE("ADSR: starts idle", "[env][adsr]")
{
    ideath::AdsrEnvelope env;
    env.prepare(kSampleRate);
    REQUIRE(env.getStage() == ideath::AdsrEnvelope::Stage::Idle);
    REQUIRE_THAT(env.process(), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("ADSR: attack rises to 1.0", "[env][adsr]")
{
    ideath::AdsrEnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.01f);    // 10ms
    env.setDecay(0.05f);
    env.setSustain(0.5f);
    env.setRelease(0.1f);
    env.noteOn();

    float peak = 0.0f;
    for (int i = 0; i < 4410; ++i) // 100ms
    {
        float v = env.process();
        if (v > peak) peak = v;
    }

    // Should have reached close to 1.0.
    REQUIRE(peak > 0.95f);
}

TEST_CASE("ADSR: sustain holds at set level", "[env][adsr]")
{
    ideath::AdsrEnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.001f);   // very fast attack
    env.setDecay(0.01f);     // fast decay
    env.setSustain(0.6f);
    env.setRelease(0.1f);
    env.noteOn();

    // Process through attack + decay.
    for (int i = 0; i < 4410; ++i)
        env.process();

    // Should be at sustain level.
    REQUIRE(env.getStage() == ideath::AdsrEnvelope::Stage::Sustain);
    float v = env.process();
    REQUIRE_THAT(v, WithinAbs(0.6f, 0.05f));
}

TEST_CASE("ADSR: release decays to zero", "[env][adsr]")
{
    ideath::AdsrEnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.001f);
    env.setDecay(0.001f);
    env.setSustain(0.5f);
    env.setRelease(0.01f);

    env.noteOn();
    for (int i = 0; i < 2000; ++i)
        env.process();

    env.noteOff();

    // Process 100ms — way more than 10ms release.
    float last = 0.0f;
    for (int i = 0; i < 4410; ++i)
        last = env.process();

    REQUIRE(last < 0.001f);
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
    REQUIRE_THAT(env.getValue(), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("ADSR: retrigger during release fades before attack", "[env][adsr]")
{
    ideath::AdsrEnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.005f);
    env.setDecay(0.05f);
    env.setSustain(0.5f);
    env.setRelease(0.1f);

    env.noteOn();
    // Advance into sustain
    for (int i = 0; i < 4410; ++i)
        env.process();
    REQUIRE(env.getStage() == ideath::AdsrEnvelope::Stage::Sustain);

    // Start release
    env.noteOff();
    for (int i = 0; i < 200; ++i)
        env.process();
    float levelBeforeRetrigger = env.getValue();
    REQUIRE(levelBeforeRetrigger > 0.01f); // still audible

    // Retrigger — should enter Retrigger stage, not Attack directly
    env.noteOn();
    REQUIRE(env.getStage() == ideath::AdsrEnvelope::Stage::Retrigger);

    // The retrigger fade should bring level smoothly toward 0
    float prev = levelBeforeRetrigger;
    bool monotonic = true;
    int samplesToZero = 0;
    for (int i = 0; i < 200; ++i)
    {
        float v = env.process();
        if (v > prev + 1e-7f) monotonic = false;
        prev = v;
        if (env.getStage() == ideath::AdsrEnvelope::Stage::Attack)
        {
            samplesToZero = i;
            break;
        }
    }

    REQUIRE(monotonic);
    // Should transition to Attack within ~2ms (88 samples at 44.1k)
    REQUIRE(samplesToZero > 0);
    REQUIRE(samplesToZero < 100);
    // Level should be near zero when attack begins
    REQUIRE(env.getValue() < 0.01f);
}

TEST_CASE("ADSR: retrigger from idle skips fade", "[env][adsr]")
{
    ideath::AdsrEnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.01f);
    env.setDecay(0.05f);
    env.setSustain(0.5f);
    env.setRelease(0.1f);

    // From idle, noteOn should go straight to Attack
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
    REQUIRE_THAT(env.process(), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("AR: attack rises to 1.0 then enters sustain", "[env][ar]")
{
    ideath::AREnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.01f);   // 10ms
    env.setRelease(0.05f);
    env.noteOn();

    float peak = 0.0f;
    for (int i = 0; i < 4410; ++i) // 100ms — way more than 10ms attack
    {
        float v = env.process();
        if (v > peak) peak = v;
    }

    REQUIRE(peak > 0.95f);
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

    REQUIRE(env.getStage() == ideath::AREnvelope::Stage::Sustain);
    for (int i = 0; i < 1000; ++i)
        REQUIRE_THAT(env.process(), WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("AR: release decays to zero and returns to idle", "[env][ar]")
{
    ideath::AREnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.001f);
    env.setRelease(0.01f);

    env.noteOn();
    for (int i = 0; i < 2000; ++i)
        env.process();
    REQUIRE(env.getStage() == ideath::AREnvelope::Stage::Sustain);

    env.noteOff();
    REQUIRE(env.getStage() == ideath::AREnvelope::Stage::Release);

    float last = 0.0f;
    for (int i = 0; i < 4410; ++i)
        last = env.process();

    REQUIRE(last < 0.001f);
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
// ADR 009 / Phase 9b1 — AdsrEnvelope curve shaping
// ---------------------------------------------------------------------------

namespace {

// Run an ADSR with the given curve and capture the level at the midpoint
// of the attack segment.
float midAttackLevel(float curve)
{
    ideath::AdsrEnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.1f);    // 100ms — 4410 samples
    env.setDecay(0.5f);
    env.setSustain(0.5f);
    env.setRelease(0.1f);
    env.setCurve(curve);
    env.noteOn();

    // Sample at i=2200 (~halfway through attack)
    float v = 0.0f;
    for (int i = 0; i < 2200; ++i)
        v = env.process();
    return v;
}

} // namespace

TEST_CASE("ADSR: Curve=0 preserves linear attack midpoint", "[env][adsr][adr009]")
{
    // Halfway through a 100ms attack, the linear ramp should be ~0.5.
    REQUIRE_THAT(midAttackLevel(0.0f), WithinAbs(0.5f, 0.05f));
}

TEST_CASE("ADSR: Curve=-1 raises attack midpoint", "[env][adsr][adr009]")
{
    // Logarithmic shape → fast initial rise → midpoint level higher than linear.
    const float linear = midAttackLevel(0.0f);
    const float log = midAttackLevel(-1.0f);
    REQUIRE(log > linear + 0.05f);
}

TEST_CASE("ADSR: Curve=+1 lowers attack midpoint", "[env][adsr][adr009]")
{
    // Exponential shape → slow initial rise → midpoint level lower than linear.
    const float linear = midAttackLevel(0.0f);
    const float exp = midAttackLevel(1.0f);
    REQUIRE(exp < linear - 0.05f);
}

TEST_CASE("ADSR: Curve clamps out-of-range input", "[env][adsr][adr009]")
{
    ideath::AdsrEnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.01f);
    env.setDecay(0.05f);
    env.setSustain(0.5f);
    env.setRelease(0.05f);

    env.setCurve(-99.0f);  // clamp to -1
    env.noteOn();
    for (int i = 0; i < 4410; ++i)
        REQUIRE(std::isfinite(env.process()));

    env.reset();
    env.setCurve(99.0f);   // clamp to +1
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
        // For curve > 0 (slow attack -> fast end), peak gets close to 1
        // but doesn't quite hit it because the attack stage exits the moment
        // the *internal* level crosses 1 — by then the curved output may
        // still be slightly below.  0.85 is a comfortable floor.
        REQUIRE(peak > 0.85f);

        env.noteOff();
        float last = 0.0f;
        for (int i = 0; i < 8820; ++i)
            last = env.process();
        REQUIRE(last < 0.001f);
    }
}

TEST_CASE("ADSR: Curve does not introduce a release jump", "[env][adsr][adr009]")
{
    // The release branch normalises against the level at noteOff so
    // sustain → release stays continuous regardless of curve.
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

    REQUIRE(env.getStage() == ideath::AdsrEnvelope::Stage::Sustain);
    float prev = env.process();
    env.noteOff();
    float first = env.process();
    REQUIRE_THAT(first, WithinAbs(prev, 0.05f));
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

    // Retrigger — check for continuity across the transition
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
    // No sudden jumps — the retrigger fade is an exponential decay (~1ms),
    // so per-sample deltas up to ~0.04 are expected.  A true click (step
    // discontinuity) would produce delta > 0.1.
    REQUIRE(maxDelta < 0.05f);
}
