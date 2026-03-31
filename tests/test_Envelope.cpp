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
