#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/FeedbackBuffer.h>
#include <cmath>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

TEST_CASE("FeedbackBuffer: stopped mode passes input through", "[feedbackbuffer]")
{
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);

    REQUIRE_THAT(fb.process(0.5f), WithinAbs(0.5f, 1e-6f));
    REQUIRE_THAT(fb.process(-0.3f), WithinAbs(-0.3f, 1e-6f));
}

TEST_CASE("FeedbackBuffer: record then play back", "[feedbackbuffer]")
{
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setMix(1.0f);

    // Record 4 samples
    fb.record();
    fb.process(0.1f);
    fb.process(0.2f);
    fb.process(0.3f);
    fb.process(0.4f);
    fb.stop();

    REQUIRE(fb.getLoopLength() == 4);

    // Play back — should loop the recorded content
    fb.play();
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.1f, 1e-6f));
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.2f, 1e-6f));
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.3f, 1e-6f));
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.4f, 1e-6f));
    // Should loop
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.1f, 1e-6f));
}

TEST_CASE("FeedbackBuffer: overdub layers new content", "[feedbackbuffer]")
{
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setFeedback(1.0f); // full feedback = keep all existing
    fb.setMix(1.0f);

    // Record 2 samples
    fb.record();
    fb.process(0.5f);
    fb.process(0.5f);
    fb.stop();

    // Overdub: add 0.1 on top
    fb.overdub();
    fb.process(0.1f);
    fb.process(0.1f);
    fb.stop();

    // Play back — should be 0.5 + 0.1 = 0.6
    fb.play();
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.6f, 1e-6f));
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.6f, 1e-6f));
}

TEST_CASE("FeedbackBuffer: overdub with feedback=0 replaces content", "[feedbackbuffer]")
{
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setMix(1.0f);

    // Record
    fb.record();
    fb.process(0.5f);
    fb.process(0.5f);
    fb.stop();

    // Overdub with feedback=0 (replace)
    fb.setFeedback(0.0f);
    fb.overdub();
    fb.process(0.1f);
    fb.process(0.2f);
    fb.stop();

    // Play — should be replaced content only
    fb.play();
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.1f, 1e-6f));
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.2f, 1e-6f));
}

TEST_CASE("FeedbackBuffer: mix blends dry and wet", "[feedbackbuffer]")
{
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setMix(0.5f);

    fb.record();
    fb.process(0.8f);
    fb.stop();

    fb.play();
    // dry=1.0*(1-0.5) + wet=0.8*0.5 = 0.5 + 0.4 = 0.9
    REQUIRE_THAT(fb.process(1.0f), WithinAbs(0.9f, 1e-6f));
}

TEST_CASE("FeedbackBuffer: reset clears everything", "[feedbackbuffer]")
{
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);

    fb.record();
    fb.process(0.5f);
    fb.stop();

    fb.reset();
    REQUIRE(fb.getLoopLength() == 0);
    REQUIRE(fb.getMode() == ideath::FeedbackBuffer::Mode::Stopped);
}

TEST_CASE("FeedbackBuffer: play/overdub ignored when no loop recorded", "[feedbackbuffer]")
{
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);

    fb.play();
    REQUIRE(fb.getMode() == ideath::FeedbackBuffer::Mode::Stopped);

    fb.overdub();
    REQUIRE(fb.getMode() == ideath::FeedbackBuffer::Mode::Stopped);
}

TEST_CASE("FeedbackBuffer: recording auto-stops at buffer end", "[feedbackbuffer]")
{
    // Use a very short buffer
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 0.001f); // ~44 samples

    fb.record();
    for (int i = 0; i < 100; ++i)
        fb.process(0.5f);

    // Should have auto-stopped and switched to Playing
    REQUIRE(fb.getMode() == ideath::FeedbackBuffer::Mode::Playing);
    REQUIRE(fb.getLoopLength() > 0);
}

TEST_CASE("FeedbackBuffer: crossfade eliminates discontinuity at loop boundary", "[feedbackbuffer]")
{
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setCrossfade(0.005f); // 5ms = 220 samples
    fb.setMix(1.0f);

    // Record a loop long enough for crossfade to engage:
    // ramp up then hold, so buffer_[end] != buffer_[0] without crossfade
    int loopLen = 2000;
    fb.record();
    for (int i = 0; i < loopLen; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(loopLen);
        fb.process(t < 0.1f ? t * 10.0f : 1.0f); // ramp 0→1, then hold 1.0
    }
    fb.stop();
    REQUIRE(fb.getLoopLength() == loopLen);

    // Play through the loop boundary and check for smooth transition
    fb.play();
    float prev = fb.process(0.0f);
    float maxJump = 0.0f;
    for (int i = 1; i < loopLen * 2; ++i)
    {
        float curr = fb.process(0.0f);
        float jump = std::fabs(curr - prev);
        if (jump > maxJump)
            maxJump = jump;
        prev = curr;
    }
    // Without crossfade, the jump at boundary would be ~1.0 (1.0 → ramp start).
    // With crossfade, max sample-to-sample jump should be much smaller.
    REQUIRE(maxJump < 0.1f);
}

TEST_CASE("FeedbackBuffer: setCrossfade changes crossfade length", "[feedbackbuffer]")
{
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setMix(1.0f);

    // Record a long enough loop
    int loopLen = 4000;
    fb.record();
    for (int i = 0; i < loopLen; ++i)
        fb.process(static_cast<float>(i) / static_cast<float>(loopLen));
    fb.stop();

    // With crossfade=0, there should be a hard jump
    fb.setCrossfade(0.0f);
    fb.play();
    float prev = fb.process(0.0f);
    float maxJump0 = 0.0f;
    for (int i = 1; i < loopLen * 2; ++i)
    {
        float curr = fb.process(0.0f);
        float jump = std::fabs(curr - prev);
        if (jump > maxJump0)
            maxJump0 = jump;
        prev = curr;
    }

    // With crossfade=10ms, the jump should be smaller
    fb.setCrossfade(0.01f);
    fb.play();
    prev = fb.process(0.0f);
    float maxJumpCF = 0.0f;
    for (int i = 1; i < loopLen * 2; ++i)
    {
        float curr = fb.process(0.0f);
        float jump = std::fabs(curr - prev);
        if (jump > maxJumpCF)
            maxJumpCF = jump;
        prev = curr;
    }

    REQUIRE(maxJumpCF < maxJump0);
}

TEST_CASE("FeedbackBuffer: feedback clamped to [0, 1]", "[feedbackbuffer]")
{
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setMix(1.0f);

    fb.setFeedback(-0.5f); // should clamp to 0

    fb.record();
    fb.process(1.0f);
    fb.stop();

    fb.overdub();
    fb.process(0.1f);
    fb.stop();

    // feedback=0 means replace: should be 0.1, not 1.1
    fb.play();
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.1f, 1e-6f));
}
