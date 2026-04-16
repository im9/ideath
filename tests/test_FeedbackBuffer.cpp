#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/FeedbackBuffer.h>
#include <cmath>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

// Threshold derivations used throughout this file
// -----------------------------------------------
// FeedbackBuffer is a looper (not a delay): it uses a double-precision
// readPos_ and int write index, with linear interpolation across the
// loopLength_ via readSample/readInterpolated. Because readPos_ is
// double, its precision is unaffected by buffer-size magnitude, so
// integer read positions produce bit-exact output from the stored buffer.
//
// Stopped mode: early return of `input` → output = input bit-exact.
//
// Playing at integer positions: readInterpolated wraps readPos_ into
// [0, loopLength) and, with frac = 0, returns s0 = readSample(i0)
// bit-exact. For short loops (≤ 2·crossfadeSamples_) readSample returns
// buffer_[i0] with no crossfade blending. Output = input·(1−mix) +
// stored·mix → for mix=1 and input=0, output = stored bit-exact.
//
// Overdub write: buf[wp] = input + existing·feedback + 1e-25f. The
// anti-denormal 1e-25f is far below float ULP at any typical loop
// amplitude (ULP of 0.5 ≈ 3e-8 ≫ 1e-25), so short-loop overdubs test
// bit-exact.
//
// Linear interp at fractional readPos: r = s0 + frac·(s1 − s0) has
// float error ≤ 2·ULP at the magnitudes used here. For the test
// values (a=0.1, b=0.2, frac=0.5), observed error is 6e-9; for
// (0.3, 0.4, 0.5) it is 2.4e-8. Tolerance 1e-7 gives 4× safety margin
// on the worst case and is 100× tighter than the old 1e-5 bound.
//
// Speed clamping: setSpeed clamps to [-4, 4] exactly (float
// representable). 1.0f and its clamped multiples are bit-exact.

TEST_CASE("FeedbackBuffer: stopped mode passes input through (bit-exact)", "[feedbackbuffer]")
{
    // Stopped mode early-returns input, so output == input exactly.
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);

    REQUIRE(fb.process(0.5f) == 0.5f);
    REQUIRE(fb.process(-0.3f) == -0.3f);
}

TEST_CASE("FeedbackBuffer: record then play back (bit-exact at integer positions)", "[feedbackbuffer]")
{
    // Short loop (4 samples) ≤ 2·crossfadeSamples → no crossfade active.
    // Integer readPos at 0, 1, 2, 3 → readInterpolated returns buffer[i]
    // bit-exact; mix=1 → output = stored sample bit-exact.
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setMix(1.0f);

    fb.record();
    fb.process(0.1f);
    fb.process(0.2f);
    fb.process(0.3f);
    fb.process(0.4f);
    fb.stop();

    REQUIRE(fb.getLoopLength() == 4);

    fb.play();
    REQUIRE(fb.process(0.0f) == 0.1f);
    REQUIRE(fb.process(0.0f) == 0.2f);
    REQUIRE(fb.process(0.0f) == 0.3f);
    REQUIRE(fb.process(0.0f) == 0.4f);
    REQUIRE(fb.process(0.0f) == 0.1f);  // wraps
}

TEST_CASE("FeedbackBuffer: overdub layers new content (bit-exact)", "[feedbackbuffer]")
{
    // fb = 1 → buf[wp] = input + existing·1 + 1e-25f ≈ input + existing.
    // Starting buf = {0.5, 0.5}, input = 0.1 across 2 calls: writes land
    // at pos 0, then pos 1 (speed=1). Result: buf = {0.6, 0.6} bit-exact
    // (1e-25 is below ULP of 0.6).
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setFeedback(1.0f);
    fb.setMix(1.0f);

    fb.record();
    fb.process(0.5f);
    fb.process(0.5f);
    fb.stop();

    fb.overdub();
    fb.process(0.1f);
    fb.process(0.1f);
    fb.stop();

    fb.play();
    REQUIRE(fb.process(0.0f) == 0.6f);
    REQUIRE(fb.process(0.0f) == 0.6f);
}

TEST_CASE("FeedbackBuffer: overdub with feedback=0 replaces content (bit-exact)", "[feedbackbuffer]")
{
    // fb = 0 → buf[wp] = input + 0 + 1e-25f. 0.1 + 1e-25 rounds to 0.1f
    // exactly in float (ULP(0.1) ≈ 7.5e-9 ≫ 1e-25).
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setMix(1.0f);

    fb.record();
    fb.process(0.5f);
    fb.process(0.5f);
    fb.stop();

    fb.setFeedback(0.0f);
    fb.overdub();
    fb.process(0.1f);
    fb.process(0.2f);
    fb.stop();

    fb.play();
    REQUIRE(fb.process(0.0f) == 0.1f);
    REQUIRE(fb.process(0.0f) == 0.2f);
}

TEST_CASE("FeedbackBuffer: mix blends dry and wet (bit-exact)", "[feedbackbuffer]")
{
    // 1.0·(1−0.5) + 0.8·0.5 = 0.5 + 0.4 = 0.9 exactly in float.
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setMix(0.5f);

    fb.record();
    fb.process(0.8f);
    fb.stop();

    fb.play();
    REQUIRE(fb.process(1.0f) == 0.9f);
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
    // maxLengthSec = 0.001 → bufferSize_ = 44 + 1 = 45 (floor · fs + 1).
    // Writing 100 samples fills the buffer and auto-transitions to Playing.
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 0.001f);

    fb.record();
    for (int i = 0; i < 100; ++i)
        fb.process(0.5f);

    REQUIRE(fb.getMode() == ideath::FeedbackBuffer::Mode::Playing);
    REQUIRE(fb.getLoopLength() == 45);  // bufferSize_ exactly
}

TEST_CASE("FeedbackBuffer: crossfade smooths loop boundary (max jump bound)", "[feedbackbuffer]")
{
    // Recorded waveform: ramp 0→1 over first 10% then hold at 1.0.
    // Without crossfade, the loop-end value 1.0 jumps to the ramp start 0.0
    // → sample-to-sample discontinuity ~1.0. A 5 ms (220 sample) crossfade
    // blends loop tail and head, capping the per-sample jump. Analytical:
    // with a linear 220-sample ramp blend between 1.0 and 0.0, per-sample
    // step ≤ 1/220 ≈ 4.5e-3. Allow 0.1 for the ramp-slope region.
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setCrossfade(0.005f);
    fb.setMix(1.0f);

    constexpr int loopLen = 2000;
    fb.record();
    for (int i = 0; i < loopLen; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(loopLen);
        fb.process(t < 0.1f ? t * 10.0f : 1.0f);
    }
    fb.stop();
    REQUIRE(fb.getLoopLength() == loopLen);

    fb.play();
    float prev = fb.process(0.0f);
    float maxJump = 0.0f;
    for (int i = 1; i < loopLen * 2; ++i)
    {
        const float curr = fb.process(0.0f);
        maxJump = std::max(maxJump, std::fabs(curr - prev));
        prev = curr;
    }
    REQUIRE(maxJump < 0.1f);
}

TEST_CASE("FeedbackBuffer: setCrossfade=0 reveals boundary jump", "[feedbackbuffer]")
{
    // With crossfade=0, ramp loop has boundary jump ≈ 1.0 (loop-end = 1.0,
    // loop-start = 0.0). With 10 ms crossfade, jump drops to the per-sample
    // slope of the blend. New test requires cf >> 0 → strictly smaller jump.
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setMix(1.0f);

    constexpr int loopLen = 4000;
    fb.record();
    for (int i = 0; i < loopLen; ++i)
        fb.process(static_cast<float>(i) / static_cast<float>(loopLen));
    fb.stop();

    auto measureMaxJump = [&]() {
        fb.play();
        float prev = fb.process(0.0f);
        float maxJump = 0.0f;
        for (int i = 1; i < loopLen * 2; ++i)
        {
            const float curr = fb.process(0.0f);
            maxJump = std::max(maxJump, std::fabs(curr - prev));
            prev = curr;
        }
        return maxJump;
    };

    fb.setCrossfade(0.0f);
    const float maxJump0 = measureMaxJump();

    fb.setCrossfade(0.01f);
    const float maxJumpCF = measureMaxJump();

    REQUIRE(maxJumpCF < maxJump0);
    REQUIRE(maxJump0 > 0.5f);    // boundary jump at 0 crossfade is large
    REQUIRE(maxJumpCF < 0.1f);   // 10 ms crossfade drops jump sharply
}

TEST_CASE("FeedbackBuffer: setSpeed default is 1.0 bit-exact", "[feedbackbuffer]")
{
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    REQUIRE(fb.getSpeed() == 1.0f);
}

TEST_CASE("FeedbackBuffer: half speed interpolates between samples", "[feedbackbuffer]")
{
    // Loop {0.1, 0.2, 0.3, 0.4}, speed=0.5 → readPos = 0, 0.5, 1, 1.5.
    // Linear interp at fractional positions. Worst ULP observed over
    // these interpolations is ≈ 2e-8 → tolerance 1e-7 with 5× margin.
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setMix(1.0f);

    fb.record();
    fb.process(0.1f); fb.process(0.2f); fb.process(0.3f); fb.process(0.4f);
    fb.stop();

    fb.setSpeed(0.5f);
    fb.play();
    REQUIRE(fb.process(0.0f) == 0.1f);                           // pos=0 exact
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.15f, 1e-7f));     // pos=0.5 lerp
    REQUIRE(fb.process(0.0f) == 0.2f);                           // pos=1 exact
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.25f, 1e-7f));     // pos=1.5 lerp
}

TEST_CASE("FeedbackBuffer: double speed skips samples", "[feedbackbuffer]")
{
    // speed=2 → integer positions 0, 2 → bit-exact samples.
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setMix(1.0f);

    fb.record();
    fb.process(0.1f); fb.process(0.2f); fb.process(0.3f); fb.process(0.4f);
    fb.stop();

    fb.setSpeed(2.0f);
    fb.play();
    REQUIRE(fb.process(0.0f) == 0.1f);
    REQUIRE(fb.process(0.0f) == 0.3f);
}

TEST_CASE("FeedbackBuffer: negative speed plays reverse (bit-exact)", "[feedbackbuffer]")
{
    // speed=-1 → starts at loopLength-1 and decrements. Integer positions,
    // bit-exact.
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setMix(1.0f);

    fb.record();
    fb.process(0.1f); fb.process(0.2f); fb.process(0.3f); fb.process(0.4f);
    fb.stop();

    fb.setSpeed(-1.0f);
    fb.play();
    REQUIRE(fb.process(0.0f) == 0.4f);
    REQUIRE(fb.process(0.0f) == 0.3f);
    REQUIRE(fb.process(0.0f) == 0.2f);
    REQUIRE(fb.process(0.0f) == 0.1f);
    REQUIRE(fb.process(0.0f) == 0.4f);  // wraps back to end
}

TEST_CASE("FeedbackBuffer: speed=0 freezes playback (bit-exact)", "[feedbackbuffer]")
{
    // readPos never advances → always returns sample at pos 0.
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setMix(1.0f);

    fb.record();
    fb.process(0.5f); fb.process(0.9f);
    fb.stop();

    fb.setSpeed(0.0f);
    fb.play();
    REQUIRE(fb.process(0.0f) == 0.5f);
    REQUIRE(fb.process(0.0f) == 0.5f);
    REQUIRE(fb.process(0.0f) == 0.5f);
}

TEST_CASE("FeedbackBuffer: speed clamps to [-4, 4] bit-exact", "[feedbackbuffer]")
{
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);

    fb.setSpeed(10.0f);
    REQUIRE(fb.getSpeed() == 4.0f);

    fb.setSpeed(-10.0f);
    REQUIRE(fb.getSpeed() == -4.0f);
}

TEST_CASE("FeedbackBuffer: half-speed reverse with interpolation", "[feedbackbuffer]")
{
    // speed=-0.5 from readPos = loopLength-1 = 3 → positions 3, 2.5, 2, 1.5.
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setMix(1.0f);

    fb.record();
    fb.process(0.1f); fb.process(0.2f); fb.process(0.3f); fb.process(0.4f);
    fb.stop();

    fb.setSpeed(-0.5f);
    fb.play();
    REQUIRE(fb.process(0.0f) == 0.4f);                           // pos=3 exact
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.35f, 1e-7f));     // pos=2.5 lerp
    REQUIRE(fb.process(0.0f) == 0.3f);                           // pos=2 exact
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.25f, 1e-7f));     // pos=1.5 lerp
}

TEST_CASE("FeedbackBuffer: speed change during playback", "[feedbackbuffer]")
{
    // Speed switch mid-stream: readPos continues from its current value.
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setMix(1.0f);

    fb.record();
    fb.process(0.1f); fb.process(0.2f); fb.process(0.3f); fb.process(0.4f);
    fb.stop();

    fb.play();
    REQUIRE(fb.process(0.0f) == 0.1f);  // pos=0 → 0.1
    fb.setSpeed(0.5f);
    REQUIRE(fb.process(0.0f) == 0.2f);  // pos=1 → 0.2 (from previous step)
    REQUIRE_THAT(fb.process(0.0f), WithinAbs(0.25f, 1e-7f));  // pos=1.5 lerp
}

TEST_CASE("FeedbackBuffer: overdub works with non-unity speed", "[feedbackbuffer]")
{
    // Overdub at half speed: first read is pos=0 → buf[0] = 0.5 bit-exact.
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setFeedback(1.0f);
    fb.setMix(1.0f);

    fb.record();
    for (int i = 0; i < 4; ++i) fb.process(0.5f);
    fb.stop();

    fb.setSpeed(0.5f);
    fb.overdub();
    REQUIRE(fb.process(0.1f) == 0.5f);  // existing (first sample read), bit-exact
}

TEST_CASE("FeedbackBuffer: overdub write position follows speed (tape-style)", "[feedbackbuffer]")
{
    // speed=0.5 → writes land at (int)0.0, (int)0.5, (int)1.0, (int)1.5
    // = 0, 0, 1, 1. So positions 2, 3 remain silent (originally 0).
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setFeedback(0.0f);
    fb.setMix(1.0f);

    fb.record();
    for (int i = 0; i < 4; ++i) fb.process(0.0f);
    fb.stop();

    fb.setSpeed(0.5f);
    fb.overdub();
    for (int i = 0; i < 4; ++i) fb.process(0.9f);
    fb.stop();

    fb.setSpeed(1.0f);
    fb.play();
    const float s0 = fb.process(0.0f);
    const float s1 = fb.process(0.0f);
    const float s2 = fb.process(0.0f);
    const float s3 = fb.process(0.0f);

    REQUIRE(s0 > 0.5f);
    REQUIRE(s1 > 0.5f);
    REQUIRE(s2 == 0.0f);  // untouched by overdub, bit-exact
    REQUIRE(s3 == 0.0f);
}

TEST_CASE("FeedbackBuffer: overdub reverse writes at reverse positions", "[feedbackbuffer]")
{
    // speed=-1 starts at pos=3 and decrements. First two writes at pos 3, 2.
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setFeedback(0.0f);
    fb.setMix(1.0f);

    fb.record();
    for (int i = 0; i < 4; ++i) fb.process(0.0f);
    fb.stop();

    fb.setSpeed(-1.0f);
    fb.overdub();
    fb.process(0.7f);  // writes at pos 3
    fb.process(0.7f);  // writes at pos 2
    fb.stop();

    fb.setSpeed(1.0f);
    fb.play();
    const float s0 = fb.process(0.0f);
    const float s1 = fb.process(0.0f);
    const float s2 = fb.process(0.0f);
    const float s3 = fb.process(0.0f);

    REQUIRE(s0 == 0.0f);  // untouched
    REQUIRE(s1 == 0.0f);  // untouched
    REQUIRE(s2 > 0.5f);
    REQUIRE(s3 > 0.5f);
}

TEST_CASE("FeedbackBuffer: overdub at speed=0 does not write (freeze)", "[feedbackbuffer]")
{
    // Explicit `if (speed_ != 0.0f)` guard in overdub — no writes at freeze.
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setFeedback(1.0f);
    fb.setMix(1.0f);

    fb.record();
    fb.process(0.5f); fb.process(0.5f);
    fb.stop();

    fb.setSpeed(0.0f);
    fb.overdub();
    for (int i = 0; i < 100; ++i) fb.process(1.0f);
    fb.stop();

    fb.setSpeed(1.0f);
    fb.play();
    REQUIRE(fb.process(0.0f) == 0.5f);
    REQUIRE(fb.process(0.0f) == 0.5f);
}

TEST_CASE("FeedbackBuffer: feedback clamped to [0, 1] (bit-exact replace on negative)", "[feedbackbuffer]")
{
    // setFeedback(-0.5) clamps to 0 → overdub replaces: buf[0] = 0.1 bit-exact.
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setMix(1.0f);

    fb.setFeedback(-0.5f);

    fb.record();
    fb.process(1.0f);
    fb.stop();

    fb.overdub();
    fb.process(0.1f);
    fb.stop();

    fb.play();
    REQUIRE(fb.process(0.0f) == 0.1f);
}

// ---------------------------------------------------------------------------
// Long-run stability
// ---------------------------------------------------------------------------

TEST_CASE("FeedbackBuffer: 10-second overdub stability at feedback=1", "[feedbackbuffer][stability]")
{
    // 10 s of continuous overdub at feedback=1 (full layering). The 1e-25
    // anti-denormal guard accumulates by ~(loop_passes · 1e-25), which
    // for 10 s / 0.1 s loop = 100 passes stays below 1e-22 — far below
    // float precision. Buffer content must stay finite and bounded.
    ideath::FeedbackBuffer fb;
    fb.prepare(kSampleRate, 1.0f);
    fb.setFeedback(1.0f);
    fb.setMix(1.0f);

    fb.record();
    for (int i = 0; i < 4410; ++i)  // 100 ms loop
        fb.process(0.1f * std::sin(2.0f * 3.14159265f * 440.0f * i / kSampleRate));
    fb.stop();

    fb.overdub();
    for (int i = 0; i < 441000; ++i)  // 10 s
    {
        const float t = static_cast<float>(i) / kSampleRate;
        const float input = 0.05f * std::sin(2.0f * 3.14159265f * 220.0f * t);
        const float out = fb.process(input);
        REQUIRE(std::isfinite(out));
        REQUIRE(std::fabs(out) <= 100.0f);  // loose — just checking no blow-up
    }
}
