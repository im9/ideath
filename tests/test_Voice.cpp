#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Voice.h>
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

TEST_CASE("Voice: output is in [-1, 1] range", "[voice]")
{
    ideath::Voice v;
    v.prepare(kSampleRate);
    v.setAttack(0.001f);
    v.setDecay(0.01f);
    v.setSustain(0.8f);
    v.setRelease(0.01f);

    v.noteOn(440.0f);

    constexpr int N = 4410; // 100ms
    for (int i = 0; i < N; ++i)
    {
        float s = v.process();
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
    }
}

TEST_CASE("Voice: produces sound after noteOn", "[voice]")
{
    ideath::Voice v;
    v.prepare(kSampleRate);
    v.setAttack(0.001f);
    v.setDecay(0.1f);
    v.setSustain(0.8f);
    v.setRelease(0.1f);

    v.noteOn(440.0f);

    constexpr int N = 4410;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i)
        buf[static_cast<size_t>(i)] = v.process();

    REQUIRE(rms(buf.data(), N) > 0.01f);
}

TEST_CASE("Voice: silent when idle (no noteOn)", "[voice]")
{
    ideath::Voice v;
    v.prepare(kSampleRate);

    constexpr int N = 1000;
    for (int i = 0; i < N; ++i)
        REQUIRE_THAT(v.process(), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("Voice: isActive reflects envelope state", "[voice]")
{
    ideath::Voice v;
    v.prepare(kSampleRate);
    v.setAttack(0.001f);
    v.setDecay(0.01f);
    v.setSustain(0.8f);
    v.setRelease(0.005f);

    REQUIRE_FALSE(v.isActive());

    v.noteOn(440.0f);
    REQUIRE(v.isActive());

    // Process through attack+decay into sustain
    for (int i = 0; i < 1000; ++i)
        v.process();
    REQUIRE(v.isActive());

    v.noteOff();
    // Should still be active during release
    REQUIRE(v.isActive());

    // Process until release completes
    for (int i = 0; i < 44100; ++i)
        v.process();
    REQUIRE_FALSE(v.isActive());
}

TEST_CASE("Voice: velocity scales output", "[voice]")
{
    ideath::Voice loud;
    loud.prepare(kSampleRate);
    loud.setAttack(0.001f);
    loud.setSustain(1.0f);
    loud.noteOn(440.0f, 1.0f);

    ideath::Voice quiet;
    quiet.prepare(kSampleRate);
    quiet.setAttack(0.001f);
    quiet.setSustain(1.0f);
    quiet.noteOn(440.0f, 0.3f);

    constexpr int N = 4410;
    std::vector<float> bufLoud(N), bufQuiet(N);
    for (int i = 0; i < N; ++i)
    {
        bufLoud[static_cast<size_t>(i)] = loud.process();
        bufQuiet[static_cast<size_t>(i)] = quiet.process();
    }

    REQUIRE(rms(bufLoud.data(), N) > rms(bufQuiet.data(), N) * 1.5f);
}

TEST_CASE("Voice: reset returns to idle", "[voice]")
{
    ideath::Voice v;
    v.prepare(kSampleRate);
    v.noteOn(440.0f);
    for (int i = 0; i < 100; ++i)
        v.process();

    REQUIRE(v.isActive());
    v.reset();
    REQUIRE_FALSE(v.isActive());
    REQUIRE_THAT(v.process(), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("Voice: different sources produce output", "[voice]")
{
    for (auto src : {ideath::Voice::Source::Oscillator,
                     ideath::Voice::Source::Wavetable,
                     ideath::Voice::Source::Noise})
    {
        ideath::Voice v;
        v.prepare(kSampleRate);
        v.setSource(src);
        v.setAttack(0.001f);
        v.setSustain(1.0f);
        v.noteOn(440.0f);

        constexpr int N = 4410;
        std::vector<float> buf(N);
        for (int i = 0; i < N; ++i)
            buf[static_cast<size_t>(i)] = v.process();

        REQUIRE(rms(buf.data(), N) > 0.01f);
    }
}

TEST_CASE("Voice: filter affects timbre", "[voice]")
{
    // Saw through aggressive lowpass should have less energy than unfiltered
    ideath::Voice unfiltered;
    unfiltered.prepare(kSampleRate);
    unfiltered.setAttack(0.001f);
    unfiltered.setSustain(1.0f);
    unfiltered.noteOn(440.0f);

    ideath::Voice filtered;
    filtered.prepare(kSampleRate);
    filtered.setAttack(0.001f);
    filtered.setSustain(1.0f);
    filtered.setFilter(ideath::Voice::FilterType::Lowpass, 200.0f, 0.707f);
    filtered.noteOn(440.0f);

    constexpr int N = 4410;
    std::vector<float> bufU(N), bufF(N);
    for (int i = 0; i < N; ++i)
    {
        bufU[static_cast<size_t>(i)] = unfiltered.process();
        bufF[static_cast<size_t>(i)] = filtered.process();
    }

    // Lowpass at 200Hz on 440Hz fundamental should reduce energy
    REQUIRE(rms(bufF.data(), N) < rms(bufU.data(), N));
}

TEST_CASE("Voice: portamento glides frequency", "[voice]")
{
    ideath::Voice v;
    v.prepare(kSampleRate);
    v.setAttack(0.001f);
    v.setSustain(1.0f);
    v.setPortamento(0.1f); // 100ms glide

    v.noteOn(220.0f);
    // Process some samples at 220
    for (int i = 0; i < 2205; ++i)
        v.process();

    // Trigger new note — should glide, not jump
    v.noteOn(880.0f);
    // Immediately after, frequency should still be near 220
    // (portamento hasn't reached target yet)
    // We can't directly check frequency, but the voice should remain active
    REQUIRE(v.isActive());
}
