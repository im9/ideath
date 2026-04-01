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
