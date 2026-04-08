#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/FMSynth.h>
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

TEST_CASE("FMSynth: output is in [-1, 1] range", "[fmsynth]")
{
    ideath::FMSynth fm;
    fm.prepare(kSampleRate);
    fm.noteOn(440.0f);

    constexpr int N = 44100; // 1 second
    for (int i = 0; i < N; ++i)
    {
        float s = fm.process();
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
    }
}

TEST_CASE("FMSynth: produces sound after noteOn", "[fmsynth]")
{
    ideath::FMSynth fm;
    fm.prepare(kSampleRate);
    fm.setAttack(0, 0.001f);
    fm.setSustain(0, 1.0f);
    fm.noteOn(440.0f);

    constexpr int N = 4410;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i)
        buf[static_cast<size_t>(i)] = fm.process();

    REQUIRE(rms(buf.data(), N) > 0.01f);
}

TEST_CASE("FMSynth: silent when idle", "[fmsynth]")
{
    ideath::FMSynth fm;
    fm.prepare(kSampleRate);

    constexpr int N = 1000;
    for (int i = 0; i < N; ++i)
        REQUIRE_THAT(fm.process(), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("FMSynth: noteOff triggers release", "[fmsynth]")
{
    ideath::FMSynth fm;
    fm.prepare(kSampleRate);
    fm.setAttack(0, 0.001f);
    fm.setSustain(0, 1.0f);
    fm.setRelease(0, 0.01f);
    fm.noteOn(440.0f);

    // Process into sustain
    for (int i = 0; i < 2000; ++i)
        fm.process();

    REQUIRE(fm.isActive());
    fm.noteOff();

    // Process until release completes
    for (int i = 0; i < 44100; ++i)
        fm.process();

    REQUIRE_FALSE(fm.isActive());
}

TEST_CASE("FMSynth: reset returns to idle", "[fmsynth]")
{
    ideath::FMSynth fm;
    fm.prepare(kSampleRate);
    fm.noteOn(440.0f);
    for (int i = 0; i < 100; ++i)
        fm.process();

    REQUIRE(fm.isActive());
    fm.reset();
    REQUIRE_FALSE(fm.isActive());
    REQUIRE_THAT(fm.process(), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("FMSynth: all 8 algorithms produce output", "[fmsynth]")
{
    for (int algo = 0; algo < ideath::FMSynth::kNumAlgorithms; ++algo)
    {
        ideath::FMSynth fm;
        fm.prepare(kSampleRate);
        fm.setAlgorithm(algo);
        for (int op = 0; op < 4; ++op)
        {
            fm.setAttack(op, 0.001f);
            fm.setSustain(op, 1.0f);
        }
        fm.noteOn(440.0f);

        constexpr int N = 4410;
        std::vector<float> buf(N);
        for (int i = 0; i < N; ++i)
            buf[static_cast<size_t>(i)] = fm.process();

        REQUIRE(rms(buf.data(), N) > 0.001f);
    }
}

TEST_CASE("FMSynth: every algorithm has a distinct routing", "[fmsynth]")
{
    // Regression: algorithms 5 and 6 used to be byte-identical (same
    // routing, different comments).  Verify all 8 algorithms produce
    // distinguishable output for the same operator settings.
    auto runAlgo = [](int algo) {
        ideath::FMSynth fm;
        fm.prepare(kSampleRate);
        fm.setAlgorithm(algo);
        for (int op = 0; op < 4; ++op)
        {
            fm.setAttack(op, 0.001f);
            fm.setDecay(op, 0.5f);
            fm.setSustain(op, 1.0f);
            fm.setRelease(op, 0.1f);
            fm.setRatio(op, 1.0f + static_cast<float>(op));
            fm.setLevel(op, 1.0f);
        }
        fm.noteOn(220.0f);

        constexpr int N = 4410;
        std::vector<float> buf(N);
        for (int i = 0; i < N; ++i)
            buf[static_cast<size_t>(i)] = fm.process();
        return buf;
    };

    std::vector<std::vector<float>> outputs;
    for (int algo = 0; algo < ideath::FMSynth::kNumAlgorithms; ++algo)
        outputs.push_back(runAlgo(algo));

    // Compare every pair: their RMS-difference must be non-trivial.
    auto rmsDiff = [](const std::vector<float>& a, const std::vector<float>& b) {
        double s = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            const double d = static_cast<double>(a[i] - b[i]);
            s += d * d;
        }
        return std::sqrt(s / static_cast<double>(a.size()));
    };

    for (int i = 0; i < ideath::FMSynth::kNumAlgorithms; ++i)
    {
        for (int j = i + 1; j < ideath::FMSynth::kNumAlgorithms; ++j)
        {
            const double d = rmsDiff(outputs[i], outputs[j]);
            INFO("rms diff between algo " << i << " and " << j << " = " << d);
            REQUIRE(d > 0.01);
        }
    }
}

TEST_CASE("FMSynth: modulator changes timbre", "[fmsynth]")
{
    // Algorithm 0: OP4→OP3→OP2→OP1→out (serial chain)
    // With mod level 0 vs 1 should differ
    ideath::FMSynth clean;
    clean.prepare(kSampleRate);
    clean.setAlgorithm(0);
    clean.setAttack(0, 0.001f);
    clean.setSustain(0, 1.0f);
    clean.setLevel(1, 0.0f); // modulator off
    clean.setLevel(2, 0.0f);
    clean.setLevel(3, 0.0f);
    clean.noteOn(440.0f);

    ideath::FMSynth modulated;
    modulated.prepare(kSampleRate);
    modulated.setAlgorithm(0);
    for (int op = 0; op < 4; ++op)
    {
        modulated.setAttack(op, 0.001f);
        modulated.setSustain(op, 1.0f);
    }
    modulated.setLevel(1, 0.8f); // modulator on
    modulated.setRatio(1, 2.0f);
    modulated.noteOn(440.0f);

    constexpr int N = 4410;
    std::vector<float> bufClean(N), bufMod(N);
    for (int i = 0; i < N; ++i)
    {
        bufClean[static_cast<size_t>(i)] = clean.process();
        bufMod[static_cast<size_t>(i)] = modulated.process();
    }

    // Modulated signal should differ from clean sine
    float diff = 0.0f;
    for (int i = 0; i < N; ++i)
        diff += std::abs(bufClean[static_cast<size_t>(i)] - bufMod[static_cast<size_t>(i)]);
    REQUIRE(diff / static_cast<float>(N) > 0.01f);
}

TEST_CASE("FMSynth: feedback adds harmonics", "[fmsynth]")
{
    ideath::FMSynth noFb;
    noFb.prepare(kSampleRate);
    noFb.setAttack(0, 0.001f);
    noFb.setSustain(0, 1.0f);
    noFb.setFeedback(0, 0.0f);
    noFb.noteOn(440.0f);

    ideath::FMSynth withFb;
    withFb.prepare(kSampleRate);
    withFb.setAttack(0, 0.001f);
    withFb.setSustain(0, 1.0f);
    withFb.setFeedback(0, 0.5f);
    withFb.noteOn(440.0f);

    constexpr int N = 4410;
    std::vector<float> bufNoFb(N), bufFb(N);
    for (int i = 0; i < N; ++i)
    {
        bufNoFb[static_cast<size_t>(i)] = noFb.process();
        bufFb[static_cast<size_t>(i)] = withFb.process();
    }

    float diff = 0.0f;
    for (int i = 0; i < N; ++i)
        diff += std::abs(bufNoFb[static_cast<size_t>(i)] - bufFb[static_cast<size_t>(i)]);
    REQUIRE(diff / static_cast<float>(N) > 0.01f);
}

TEST_CASE("FMSynth: velocity scales output", "[fmsynth]")
{
    ideath::FMSynth loud;
    loud.prepare(kSampleRate);
    loud.setAttack(0, 0.001f);
    loud.setSustain(0, 1.0f);
    loud.noteOn(440.0f, 1.0f);

    ideath::FMSynth quiet;
    quiet.prepare(kSampleRate);
    quiet.setAttack(0, 0.001f);
    quiet.setSustain(0, 1.0f);
    quiet.noteOn(440.0f, 0.3f);

    constexpr int N = 4410;
    std::vector<float> bufL(N), bufQ(N);
    for (int i = 0; i < N; ++i)
    {
        bufL[static_cast<size_t>(i)] = loud.process();
        bufQ[static_cast<size_t>(i)] = quiet.process();
    }

    REQUIRE(rms(bufL.data(), N) > rms(bufQ.data(), N) * 1.5f);
}

TEST_CASE("FMSynth: ratio changes pitch", "[fmsynth]")
{
    // Count zero crossings at ratio 1.0 vs 2.0
    auto countCrossings = [](float ratio)
    {
        ideath::FMSynth fm;
        fm.prepare(kSampleRate);
        fm.setAlgorithm(7); // all carriers (additive)
        fm.setLevel(0, 1.0f);
        fm.setLevel(1, 0.0f);
        fm.setLevel(2, 0.0f);
        fm.setLevel(3, 0.0f);
        fm.setRatio(0, ratio);
        fm.setAttack(0, 0.001f);
        fm.setSustain(0, 1.0f);
        fm.noteOn(440.0f);

        // Skip attack
        for (int i = 0; i < 200; ++i) fm.process();

        int crossings = 0;
        float prev = fm.process();
        constexpr int N = 4410;
        for (int i = 0; i < N; ++i)
        {
            float s = fm.process();
            if ((prev >= 0.0f && s < 0.0f) || (prev < 0.0f && s >= 0.0f))
                ++crossings;
            prev = s;
        }
        return crossings;
    };

    int cross1 = countCrossings(1.0f);
    int cross2 = countCrossings(2.0f);

    // Ratio 2.0 should have ~2x the zero crossings
    REQUIRE(cross2 > cross1 * 1.8);
    REQUIRE(cross2 < cross1 * 2.2);
}

TEST_CASE("FMSynth: algorithm clamps to valid range", "[fmsynth]")
{
    ideath::FMSynth fm;
    fm.prepare(kSampleRate);

    fm.setAlgorithm(-1);
    REQUIRE(fm.getAlgorithm() == 0);

    fm.setAlgorithm(99);
    REQUIRE(fm.getAlgorithm() == 7);
}

TEST_CASE("FMSynth: operator index out of range is safe", "[fmsynth]")
{
    ideath::FMSynth fm;
    fm.prepare(kSampleRate);

    // Should not crash
    fm.setRatio(-1, 2.0f);
    fm.setRatio(4, 2.0f);
    fm.setLevel(99, 0.5f);
    fm.setFeedback(-1, 0.3f);
    fm.setAttack(4, 0.1f);
}
