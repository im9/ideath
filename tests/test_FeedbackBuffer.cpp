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

TEST_CASE("FeedbackBuffer: setSpeed default is 1.0", "[feedbackbuffer]")
{
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    REQUIRE_THAT(fb.getSpeed(), WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("FeedbackBuffer: half speed doubles playback duration", "[feedbackbuffer]")
{
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setMix(1.0f);

    // Record 4 samples: 0.1, 0.2, 0.3, 0.4
    fb.record();
    fb.process(0.1f);
    fb.process(0.2f);
    fb.process(0.3f);
    fb.process(0.4f);
    fb.stop();

    // Play at half speed — should interpolate between samples
    fb.setSpeed(0.5f);
    fb.play();
    // readPos advances 0.5 each sample, so:
    //   pos=0.0 → 0.1
    //   pos=0.5 → lerp(0.1, 0.2) = 0.15
    //   pos=1.0 → 0.2
    //   pos=1.5 → lerp(0.2, 0.3) = 0.25
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.1f, 1e-5f));
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.15f, 1e-5f));
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.2f, 1e-5f));
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.25f, 1e-5f));
}

TEST_CASE("FeedbackBuffer: double speed halves playback duration", "[feedbackbuffer]")
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

    // Play at double speed — skips every other sample
    fb.setSpeed(2.0f);
    fb.play();
    // pos=0.0 → 0.1, pos=2.0 → 0.3
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.1f, 1e-5f));
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.3f, 1e-5f));
}

TEST_CASE("FeedbackBuffer: negative speed plays in reverse", "[feedbackbuffer]")
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

    // Play in reverse — starts from end of loop
    fb.setSpeed(-1.0f);
    fb.play();
    // Reverse: starts at loopLength-1 = 3, goes backward
    //   pos=3 → 0.4, pos=2 → 0.3, pos=1 → 0.2, pos=0 → 0.1
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.4f, 1e-5f));
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.3f, 1e-5f));
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.2f, 1e-5f));
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.1f, 1e-5f));
    // Wraps: back to end
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.4f, 1e-5f));
}

TEST_CASE("FeedbackBuffer: speed=0 freezes playback position", "[feedbackbuffer]")
{
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setMix(1.0f);

    fb.record();
    fb.process(0.5f);
    fb.process(0.9f);
    fb.stop();

    fb.setSpeed(0.0f);
    fb.play();
    // Frozen at position 0 — always returns first sample
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.5f, 1e-5f));
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.5f, 1e-5f));
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.5f, 1e-5f));
}

TEST_CASE("FeedbackBuffer: speed clamped to [-4, 4]", "[feedbackbuffer]")
{
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);

    fb.setSpeed(10.0f);
    REQUIRE_THAT(fb.getSpeed(), WithinAbs(4.0f, 1e-6f));

    fb.setSpeed(-10.0f);
    REQUIRE_THAT(fb.getSpeed(), WithinAbs(-4.0f, 1e-6f));
}

TEST_CASE("FeedbackBuffer: half-speed reverse with interpolation", "[feedbackbuffer]")
{
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setMix(1.0f);

    fb.record();
    fb.process(0.1f);
    fb.process(0.2f);
    fb.process(0.3f);
    fb.process(0.4f);
    fb.stop();

    fb.setSpeed(-0.5f);
    fb.play();
    // Starts at end (pos=3), moves -0.5 each sample
    //   pos=3.0 → 0.4
    //   pos=2.5 → lerp(0.3, 0.4) = 0.35
    //   pos=2.0 → 0.3
    //   pos=1.5 → lerp(0.2, 0.3) = 0.25
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.4f, 1e-5f));
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.35f, 1e-5f));
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.3f, 1e-5f));
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.25f, 1e-5f));
}

TEST_CASE("FeedbackBuffer: speed change during playback", "[feedbackbuffer]")
{
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setMix(1.0f);

    fb.record();
    fb.process(0.1f);
    fb.process(0.2f);
    fb.process(0.3f);
    fb.process(0.4f);
    fb.stop();

    fb.play();
    // Normal speed: read sample 0
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.1f, 1e-5f));
    // Now at pos=1.0. Switch to half speed
    fb.setSpeed(0.5f);
    // pos=1.0 → 0.2
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.2f, 1e-5f));
    // pos=1.5 → lerp(0.2, 0.3) = 0.25
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.25f, 1e-5f));
}

TEST_CASE("FeedbackBuffer: overdub works with non-unity speed", "[feedbackbuffer]")
{
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setFeedback(1.0f);
    fb.setMix(1.0f);

    fb.record();
    fb.process(0.5f);
    fb.process(0.5f);
    fb.process(0.5f);
    fb.process(0.5f);
    fb.stop();

    // Overdub at half speed — both read and write advance at 0.5x (tape-style)
    fb.setSpeed(0.5f);
    fb.overdub();
    float out0 = fb.process(0.1f);
    // Read from pos=0.0 → 0.5, output should be 0.5
    REQUIRE_THAT(out0, WithinAbs(0.5f, 1e-5f));
}

TEST_CASE("FeedbackBuffer: overdub write position follows speed (tape-style)", "[feedbackbuffer]")
{
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setFeedback(0.0f); // replace mode
    fb.setMix(1.0f);

    // Record 4 samples of silence
    fb.record();
    for (int i = 0; i < 4; ++i) fb.process(0.0f);
    fb.stop();

    // Overdub at half speed — both heads move at 0.5x
    // readPos: 0.0, 0.5, 1.0, 1.5 → write to int positions: 0, 0, 1, 1
    fb.setSpeed(0.5f);
    fb.overdub();
    for (int i = 0; i < 4; ++i) fb.process(0.9f);
    fb.stop();

    // Play at 1x — only positions 0,1 were written; 2,3 remain silent
    fb.setSpeed(1.0f);
    fb.play();
    float s0 = fb.process(0.0f);
    float s1 = fb.process(0.0f);
    float s2 = fb.process(0.0f);
    float s3 = fb.process(0.0f);

    REQUIRE(s0 > 0.5f);
    REQUIRE(s1 > 0.5f);
    REQUIRE_THAT(s2, WithinAbs(0.0f, 1e-5f));
    REQUIRE_THAT(s3, WithinAbs(0.0f, 1e-5f));
}

TEST_CASE("FeedbackBuffer: overdub reverse writes at reverse position", "[feedbackbuffer]")
{
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setFeedback(0.0f); // replace mode
    fb.setMix(1.0f);

    // Record 4 samples of silence
    fb.record();
    for (int i = 0; i < 4; ++i) fb.process(0.0f);
    fb.stop();

    // Overdub in reverse — starts at end, writes backward
    fb.setSpeed(-1.0f);
    fb.overdub();
    fb.process(0.7f); // writes at pos 3
    fb.process(0.7f); // writes at pos 2
    fb.stop();

    // Play forward — positions 2,3 written, 0,1 untouched
    fb.setSpeed(1.0f);
    fb.play();
    float s0 = fb.process(0.0f);
    float s1 = fb.process(0.0f);
    float s2 = fb.process(0.0f);
    float s3 = fb.process(0.0f);

    REQUIRE_THAT(s0, WithinAbs(0.0f, 1e-5f));
    REQUIRE_THAT(s1, WithinAbs(0.0f, 1e-5f));
    REQUIRE(s2 > 0.5f);
    REQUIRE(s3 > 0.5f);
}

TEST_CASE("FeedbackBuffer: overdub at speed=0 does not write (freeze)", "[feedbackbuffer]")
{
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setFeedback(1.0f);
    fb.setMix(1.0f);

    // Record a known value
    fb.record();
    fb.process(0.5f);
    fb.process(0.5f);
    fb.stop();

    // Overdub at speed=0 with loud input — buffer should NOT accumulate
    fb.setSpeed(0.0f);
    fb.overdub();
    for (int i = 0; i < 100; ++i)
        fb.process(1.0f);
    fb.stop();

    // Play back — content should still be the original 0.5
    fb.setSpeed(1.0f);
    fb.play();
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.5f, 1e-5f));
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.5f, 1e-5f));
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
