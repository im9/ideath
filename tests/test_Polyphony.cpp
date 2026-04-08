#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Polyphony.h>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

static float rms(const float* buf, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += static_cast<double>(buf[i]) * static_cast<double>(buf[i]);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(n)));
}

TEST_CASE("Polyphony: silent with no notes", "[poly]")
{
    ideath::Polyphony poly;
    poly.prepare(kSampleRate, 8);

    for (int i = 0; i < 1000; ++i)
        REQUIRE_THAT(poly.process(), WithinAbs(0.0f, 1e-6f));

    REQUIRE(poly.getActiveVoiceCount() == 0);
    REQUIRE_FALSE(poly.hasActiveVoices());
}

TEST_CASE("Polyphony: single note produces sound", "[poly]")
{
    ideath::Polyphony poly;
    poly.prepare(kSampleRate, 8);
    poly.setAttack(0.001f);
    poly.setSustain(1.0f);

    poly.noteOn(440.0f);
    REQUIRE(poly.getActiveVoiceCount() == 1);

    constexpr int N = 4410;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i)
        buf[static_cast<size_t>(i)] = poly.process();

    REQUIRE(rms(buf.data(), N) > 0.01f);
}

TEST_CASE("Polyphony: multiple notes stack", "[poly]")
{
    ideath::Polyphony poly;
    poly.prepare(kSampleRate, 8);
    poly.setAttack(0.001f);
    poly.setSustain(1.0f);

    poly.noteOn(261.63f); // C4
    poly.noteOn(329.63f); // E4
    poly.noteOn(392.00f); // G4

    REQUIRE(poly.getActiveVoiceCount() == 3);
    REQUIRE(poly.hasActiveVoices());
}

TEST_CASE("Polyphony: noteOff releases matching voice", "[poly]")
{
    ideath::Polyphony poly;
    poly.prepare(kSampleRate, 8);
    poly.setAttack(0.001f);
    poly.setSustain(1.0f);
    poly.setRelease(0.005f);

    poly.noteOn(440.0f);
    poly.noteOn(880.0f);
    REQUIRE(poly.getActiveVoiceCount() == 2);

    poly.noteOff(440.0f);
    // Voice is still active during release
    REQUIRE(poly.getActiveVoiceCount() == 2);

    // Wait for release to finish
    for (int i = 0; i < 44100; ++i)
        poly.process();

    // 440Hz voice should be idle now, 880Hz still sustaining
    REQUIRE(poly.getActiveVoiceCount() == 1);
}

TEST_CASE("Polyphony: allNotesOff releases everything", "[poly]")
{
    ideath::Polyphony poly;
    poly.prepare(kSampleRate, 4);
    poly.setAttack(0.001f);
    poly.setSustain(1.0f);
    poly.setRelease(0.005f);

    poly.noteOn(261.63f);
    poly.noteOn(329.63f);
    poly.noteOn(392.00f);
    REQUIRE(poly.getActiveVoiceCount() == 3);

    poly.allNotesOff();

    // Wait for releases
    for (int i = 0; i < 44100; ++i)
        poly.process();

    REQUIRE(poly.getActiveVoiceCount() == 0);
}

TEST_CASE("Polyphony: voice stealing when pool exhausted", "[poly]")
{
    ideath::Polyphony poly;
    poly.prepare(kSampleRate, 4); // only 4 voices
    poly.setAttack(0.001f);
    poly.setSustain(1.0f);

    poly.noteOn(261.63f);
    poly.noteOn(329.63f);
    poly.noteOn(392.00f);
    poly.noteOn(523.25f);
    REQUIRE(poly.getActiveVoiceCount() == 4);

    // 5th note should steal the oldest voice
    poly.noteOn(659.25f);
    REQUIRE(poly.getActiveVoiceCount() == 4); // still 4, not 5
}

TEST_CASE("Polyphony: output is in [-1, 1] range", "[poly]")
{
    ideath::Polyphony poly;
    poly.prepare(kSampleRate, 8);
    poly.setAttack(0.001f);
    // Sustain at full level — the soft-saturated mix bus must keep the
    // chord under unity without us having to baby the envelope.
    poly.setSustain(1.0f);

    // Play a chord
    poly.noteOn(261.63f, 0.5f);
    poly.noteOn(329.63f, 0.5f);
    poly.noteOn(392.00f, 0.5f);

    constexpr int N = 4410;
    for (int i = 0; i < N; ++i)
    {
        float s = poly.process();
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
    }
}

TEST_CASE("Polyphony: heavy chord saturates softly without flat-topping", "[poly]")
{
    // Regression: the mix bus used to hard-clip the raw voice sum to
    // [-1, 1], producing flat-topped output (many consecutive samples
    // pinned at exactly ±1) and harsh harmonics.  After switching to a
    // tanh soft saturator the output should still be bounded but should
    // never sit pinned at the rail.
    ideath::Polyphony poly;
    poly.prepare(kSampleRate, 8);
    poly.setAttack(0.001f);
    poly.setSustain(1.0f);

    // 8-voice chord — each voice is a saw at velocity 1.0, summed sum
    // would peak around ±5..6 before saturation.
    const float freqs[8] = { 130.81f, 164.81f, 196.00f, 246.94f,
                              261.63f, 329.63f, 392.00f, 493.88f };
    for (float f : freqs)
        poly.noteOn(f, 1.0f);

    // Skip the attack window
    for (int i = 0; i < 2048; ++i)
        poly.process();

    constexpr int N = 8192;
    int exactRailHits = 0;
    int flatRunPairs = 0;
    float prev = poly.process();
    for (int i = 1; i < N; ++i)
    {
        float s = poly.process();
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
        // Hard clip emits the literal value 1.0f (or -1.0f) for every
        // sample whose pre-clip sum is outside [-1, 1].  tanh approaches
        // ±1 asymptotically but, for the float magnitudes summed here
        // (~5..6), never produces the *exact* IEEE 1.0f.
        if (s == 1.0f || s == -1.0f)
            ++exactRailHits;
        // Hard clip also flattens consecutive samples whenever the input
        // stays clipped — the derivative goes to zero.  tanh's derivative
        // never reaches zero for finite input, so consecutive identical
        // floats are essentially impossible on a non-trivial waveform.
        if (s == prev)
            ++flatRunPairs;
        prev = s;
    }
    INFO("exact rail hits: " << exactRailHits
         << "  flat-run pairs: " << flatRunPairs << " / " << N);
    REQUIRE(exactRailHits == 0);
    REQUIRE(flatRunPairs < 4);
}

TEST_CASE("Polyphony: reset clears all voices", "[poly]")
{
    ideath::Polyphony poly;
    poly.prepare(kSampleRate, 8);
    poly.setAttack(0.001f);
    poly.setSustain(1.0f);

    poly.noteOn(440.0f);
    poly.noteOn(880.0f);
    REQUIRE(poly.getActiveVoiceCount() == 2);

    poly.reset();
    REQUIRE(poly.getActiveVoiceCount() == 0);
    REQUIRE_THAT(poly.process(), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("Polyphony: chord is louder than single note", "[poly]")
{
    // Single note
    ideath::Polyphony single;
    single.prepare(kSampleRate, 8);
    single.setAttack(0.001f);
    single.setSustain(1.0f);
    single.noteOn(440.0f);

    // Chord
    ideath::Polyphony chord;
    chord.prepare(kSampleRate, 8);
    chord.setAttack(0.001f);
    chord.setSustain(1.0f);
    chord.noteOn(261.63f);
    chord.noteOn(329.63f);
    chord.noteOn(392.00f);

    constexpr int N = 4410;
    std::vector<float> bufS(N), bufC(N);
    for (int i = 0; i < N; ++i)
    {
        bufS[static_cast<size_t>(i)] = single.process();
        bufC[static_cast<size_t>(i)] = chord.process();
    }

    REQUIRE(rms(bufC.data(), N) > rms(bufS.data(), N));
}

TEST_CASE("Polyphony: getMaxVoices returns configured count", "[poly]")
{
    ideath::Polyphony poly;
    poly.prepare(kSampleRate, 12);
    REQUIRE(poly.getMaxVoices() == 12);
}
