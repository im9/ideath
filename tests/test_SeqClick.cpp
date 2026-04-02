#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <vector>
#include <algorithm>

// Replicate the REPL AudioEngine signal chain offline to detect clicks
#include <ideath/Oscillator.h>
#include <ideath/Envelope.h>
#include <ideath/Biquad.h>
#include <ideath/Portamento.h>
#include <ideath/Saturation.h>

using namespace ideath;

TEST_CASE("Sequencer note transitions have no clicks (saw, no envelope)", "[seqclick]")
{
    constexpr float kSR = 44100.0f;
    constexpr float kBPM = 140.0f;
    constexpr int kSamplesPerStep = static_cast<int>(kSR * 60.0f / kBPM);
    constexpr int kGateSamples = kSamplesPerStep * 80 / 100;
    constexpr float kVelocity = 0.7f;

    // Frequencies: C3 C3 - Eb3 C3 C3 - C3
    const float kC3 = 130.81f;
    const float kEb3 = 155.56f;
    float seqFreqs[] = {kC3, kC3, 0.0f, kEb3, kC3, kC3, 0.0f, kC3};
    constexpr int kNumSteps = 8;

    Oscillator osc;
    osc.prepare(kSR);
    osc.setFrequency(kC3);

    Portamento gainSmoother;
    gainSmoother.prepare(kSR);
    gainSmoother.setTime(0.005f);
    gainSmoother.setValue(0.0f);
    gainSmoother.setTarget(0.0f);

    int step = 0;
    int sampleCounter = 0;
    bool gateOpen = false;

    // Run for 2 full cycles of the sequence
    int totalSamples = kSamplesPerStep * kNumSteps * 2;
    std::vector<float> output(totalSamples);

    // Track max delta excluding saw wave resets
    // We detect clicks by looking at envelope of the signal (low-pass filtered delta)
    float maxDeltaAtTransition = 0.0f;

    // Trigger first note
    float freq = seqFreqs[0];
    osc.setFrequency(freq);
    gainSmoother.setTarget(kVelocity);
    gateOpen = true;

    for (int i = 0; i < totalSamples; ++i)
    {
        // Gate off
        if (gateOpen && sampleCounter >= kGateSamples)
        {
            gainSmoother.setTarget(0.0f);
            gateOpen = false;
        }

        ++sampleCounter;
        if (sampleCounter >= kSamplesPerStep)
        {
            sampleCounter = 0;
            step = (step + 1) % kNumSteps;

            float f = seqFreqs[step];
            if (f > 0.0f)
            {
                osc.setFrequency(f);
                gainSmoother.setTarget(kVelocity);
                gateOpen = true;
            }
            else
            {
                gainSmoother.setTarget(0.0f);
            }
        }

        float gain = gainSmoother.process();
        float sample = osc.process(1.0f); // saw
        output[i] = sample * gain * 0.5f;
    }

    // Analyze: compute deltas, ignoring saw wave resets
    // A click at a transition would show as a large delta in the ENVELOPE
    // (gain * sample), not just the raw waveform.
    // Strategy: low-pass filter the output, then check for large deltas
    // in the filtered signal around transition points.

    // Simple envelope follower (peak hold with release)
    std::vector<float> envelope(totalSamples);
    float envLevel = 0.0f;
    float envAttack = 1.0f - std::exp(-5.0f / (0.001f * kSR)); // 1ms attack
    float envRelease = 1.0f - std::exp(-5.0f / (0.010f * kSR)); // 10ms release
    for (int i = 0; i < totalSamples; ++i)
    {
        float absVal = std::fabs(output[i]);
        if (absVal > envLevel)
            envLevel += envAttack * (absVal - envLevel);
        else
            envLevel += envRelease * (absVal - envLevel);
        envelope[i] = envLevel;
    }

    // Check envelope deltas around each note-on transition
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        for (int s = 0; s < kNumSteps; ++s)
        {
            if (seqFreqs[s] <= 0.0f) continue; // skip rests

            int transitionSample = (cycle * kNumSteps + s) * kSamplesPerStep;
            if (transitionSample <= 0 || transitionSample >= totalSamples - 100) continue;

            // Check envelope delta in a window around the transition
            for (int j = transitionSample - 5; j < transitionSample + 50; ++j)
            {
                if (j <= 0 || j >= totalSamples) continue;
                float delta = std::fabs(envelope[j] - envelope[j - 1]);
                maxDeltaAtTransition = std::max(maxDeltaAtTransition, delta);
            }
        }
    }

    INFO("Max envelope delta at note transitions: " << maxDeltaAtTransition);

    // A smooth transition should have envelope delta < 0.01
    // A click would produce a delta > 0.05
    REQUIRE(maxDeltaAtTransition < 0.02f);
}

TEST_CASE("Sequencer note transitions have no clicks (saw + acid filter)", "[seqclick]")
{
    constexpr float kSR = 44100.0f;
    constexpr float kBPM = 140.0f;
    constexpr int kSamplesPerStep = static_cast<int>(kSR * 60.0f / kBPM);
    constexpr int kGateSamples = kSamplesPerStep * 80 / 100;

    const float kC3 = 130.81f;
    const float kEb3 = 155.56f;
    float seqFreqs[] = {kC3, kC3, 0.0f, kEb3, kC3, kC3, 0.0f, kC3};
    constexpr int kNumSteps = 8;

    Oscillator osc;
    osc.prepare(kSR);
    osc.setFrequency(kC3);

    AdsrEnvelope env;
    env.prepare(kSR);
    env.setAttack(0.005f);
    env.setDecay(0.15f);
    env.setSustain(0.0f);
    env.setRelease(0.05f);

    Biquad filter;
    filter.setLowpass(400.0f, 8.0f, kSR);

    Portamento gainSmoother;
    gainSmoother.prepare(kSR);
    gainSmoother.setTime(0.005f);
    gainSmoother.setValue(0.0f);
    gainSmoother.setTarget(0.0f);

    int step = 0;
    int sampleCounter = 0;
    bool gateOpen = false;

    int totalSamples = kSamplesPerStep * kNumSteps * 2;
    std::vector<float> output(totalSamples);

    // Trigger first note
    osc.setFrequency(seqFreqs[0]);
    gainSmoother.setTarget(0.7f);
    env.noteOn();
    gateOpen = true;

    for (int i = 0; i < totalSamples; ++i)
    {
        // Gate off
        if (gateOpen && sampleCounter >= kGateSamples)
        {
            env.noteOff();
            gainSmoother.setTarget(0.0f);
            gateOpen = false;
        }

        ++sampleCounter;
        if (sampleCounter >= kSamplesPerStep)
        {
            sampleCounter = 0;
            step = (step + 1) % kNumSteps;

            float f = seqFreqs[step];
            if (f > 0.0f)
            {
                osc.setFrequency(f);
                gainSmoother.setTarget(0.7f);
                env.noteOn();
                gateOpen = true;
            }
            else
            {
                gainSmoother.setTarget(0.0f);
            }
        }

        float gain = gainSmoother.process();
        float sample = osc.process(1.0f);
        float envVal = env.process();
        sample *= envVal;
        sample = filter.process(sample);
        sample = Saturation::tanhDrive(sample, 2.0f);
        output[i] = sample * gain * 0.5f;
    }

    // Envelope follower
    std::vector<float> envelope(totalSamples);
    float envLevel = 0.0f;
    float envAttack = 1.0f - std::exp(-5.0f / (0.001f * kSR));
    float envRelease = 1.0f - std::exp(-5.0f / (0.010f * kSR));
    for (int i = 0; i < totalSamples; ++i)
    {
        float absVal = std::fabs(output[i]);
        if (absVal > envLevel)
            envLevel += envAttack * (absVal - envLevel);
        else
            envLevel += envRelease * (absVal - envLevel);
        envelope[i] = envLevel;
    }

    // Check deltas at transitions
    float maxDeltaAtTransition = 0.0f;
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        for (int s = 0; s < kNumSteps; ++s)
        {
            if (seqFreqs[s] <= 0.0f) continue;
            int transitionSample = (cycle * kNumSteps + s) * kSamplesPerStep;
            if (transitionSample <= 0 || transitionSample >= totalSamples - 100) continue;

            for (int j = transitionSample - 5; j < transitionSample + 50; ++j)
            {
                if (j <= 0 || j >= totalSamples) continue;
                float delta = std::fabs(envelope[j] - envelope[j - 1]);
                maxDeltaAtTransition = std::max(maxDeltaAtTransition, delta);
            }
        }
    }

    INFO("Max envelope delta at note transitions (acid): " << maxDeltaAtTransition);
    REQUIRE(maxDeltaAtTransition < 0.02f);
}
