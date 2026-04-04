#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <vector>
#include <algorithm>

// Replicate the REPL AudioEngine signal chain offline to detect clicks
#include <ideath/Oscillator.h>
#include <ideath/Envelope.h>
#include <ideath/SVFilter.h>
#include <ideath/Portamento.h>
#include <ideath/Saturation.h>

using namespace ideath;

// Shared envelope-follower click detector.
// Returns the max envelope delta found around note-on transitions.
static float measureClickDelta(const std::vector<float>& output,
                               const float* seqFreqs, int numSteps,
                               int samplesPerStep, int cycles, float sr)
{
    const int totalSamples = static_cast<int>(output.size());

    // Envelope follower (peak hold with release)
    std::vector<float> envelope(totalSamples);
    float envLevel = 0.0f;
    const float envAttack  = 1.0f - std::exp(-5.0f / (0.001f * sr)); // 1ms
    const float envRelease = 1.0f - std::exp(-5.0f / (0.010f * sr)); // 10ms
    for (int i = 0; i < totalSamples; ++i)
    {
        float absVal = std::fabs(output[i]);
        if (absVal > envLevel)
            envLevel += envAttack * (absVal - envLevel);
        else
            envLevel += envRelease * (absVal - envLevel);
        envelope[i] = envLevel;
    }

    float maxDelta = 0.0f;
    for (int cycle = 0; cycle < cycles; ++cycle)
    {
        for (int s = 0; s < numSteps; ++s)
        {
            if (seqFreqs[s] <= 0.0f) continue;
            int transitionSample = (cycle * numSteps + s) * samplesPerStep;
            if (transitionSample <= 0 || transitionSample >= totalSamples - 100) continue;

            for (int j = transitionSample - 5; j < transitionSample + 50; ++j)
            {
                if (j <= 0 || j >= totalSamples) continue;
                float delta = std::fabs(envelope[j] - envelope[j - 1]);
                maxDelta = std::max(maxDelta, delta);
            }
        }
    }
    return maxDelta;
}

TEST_CASE("Sequencer note transitions have no clicks (saw, no envelope)", "[seqclick]")
{
    constexpr float kSR = 44100.0f;
    constexpr float kBPM = 140.0f;
    constexpr int kSamplesPerStep = static_cast<int>(kSR * 60.0f / kBPM);
    constexpr int kGateSamples = kSamplesPerStep * 80 / 100;
    constexpr float kVelocity = 0.7f;

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

    int totalSamples = kSamplesPerStep * kNumSteps * 2;
    std::vector<float> output(totalSamples);

    // Trigger first note
    osc.setFrequency(seqFreqs[0]);
    gainSmoother.setTarget(kVelocity);
    gateOpen = true;

    for (int i = 0; i < totalSamples; ++i)
    {
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

    float maxDelta = measureClickDelta(output, seqFreqs, kNumSteps,
                                       kSamplesPerStep, 2, kSR);
    INFO("Max envelope delta at note transitions: " << maxDelta);
    REQUIRE(maxDelta < 0.02f);
}

TEST_CASE("Sequencer note transitions have no clicks (saw + acid SVFilter)", "[seqclick]")
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

    // SVFilter replaces Biquad — matches REPL AudioEngine.
    SVFilter filter;
    filter.prepare(kSR);
    filter.setCutoff(400.0f);
    // Q=8 mapped to SVFilter resonance: (1 - 0.707/8) * 0.9 ≈ 0.82
    filter.setResonance((1.0f - (0.707f / 8.0f)) * 0.9f);
    filter.setMode(SVFilter::Mode::Lowpass);

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

        // Signal chain matches REPL: Osc → Filter → Envelope → Saturation
        sample = filter.process(sample);
        float envVal = env.process();
        sample *= envVal;
        sample = Saturation::tanhDrive(sample, 2.0f);
        output[i] = sample * gain * 0.5f;
    }

    float maxDelta = measureClickDelta(output, seqFreqs, kNumSteps,
                                       kSamplesPerStep, 2, kSR);
    INFO("Max envelope delta at note transitions (acid): " << maxDelta);
    REQUIRE(maxDelta < 0.02f);
}

TEST_CASE("Sequencer retrigger with high resonance has no clicks", "[seqclick]")
{
    // Stress test: high resonance, short notes, large interval jumps.
    constexpr float kSR = 44100.0f;
    constexpr float kBPM = 160.0f;
    constexpr int kSamplesPerStep = static_cast<int>(kSR * 60.0f / kBPM);
    constexpr int kGateSamples = kSamplesPerStep * 70 / 100;

    const float kC3 = 130.81f;
    const float kG4 = 392.00f;
    const float kC5 = 523.25f;
    // Wide interval jumps to stress the filter
    float seqFreqs[] = {kC3, kG4, kC5, kC3, kG4, 0.0f, kC3, kC5};
    constexpr int kNumSteps = 8;

    Oscillator osc;
    osc.prepare(kSR);
    osc.setFrequency(kC3);

    AdsrEnvelope env;
    env.prepare(kSR);
    env.setAttack(0.003f);   // very fast attack
    env.setDecay(0.10f);
    env.setSustain(0.0f);
    env.setRelease(0.03f);   // short release — retrigger during decay

    SVFilter filter;
    filter.prepare(kSR);
    filter.setCutoff(600.0f);
    // High resonance: Q=15 → (1 - 0.707/15) * 0.9 ≈ 0.86
    filter.setResonance((1.0f - (0.707f / 15.0f)) * 0.9f);
    filter.setMode(SVFilter::Mode::Lowpass);

    Portamento gainSmoother;
    gainSmoother.prepare(kSR);
    gainSmoother.setTime(0.005f);
    gainSmoother.setValue(0.0f);
    gainSmoother.setTarget(0.0f);

    int step = 0;
    int sampleCounter = 0;
    bool gateOpen = false;

    int totalSamples = kSamplesPerStep * kNumSteps * 3; // 3 cycles
    std::vector<float> output(totalSamples);

    // Trigger first note
    osc.setFrequency(seqFreqs[0]);
    gainSmoother.setTarget(1.0f);
    env.noteOn();
    gateOpen = true;

    for (int i = 0; i < totalSamples; ++i)
    {
        if (gateOpen && sampleCounter >= kGateSamples)
        {
            env.noteOff();
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
                gainSmoother.setValue(1.0f);
                env.noteOn();
                gateOpen = true;
            }
            else
            {
                env.noteOff();
                gateOpen = false;
            }
        }

        float gain = gainSmoother.process();
        float sample = osc.process(1.0f);

        // Osc → Filter → Envelope → Saturation
        sample = filter.process(sample);
        float envVal = env.process();
        sample *= envVal;
        sample = Saturation::tanhDrive(sample, 3.0f); // higher drive
        output[i] = sample * gain * 0.5f;
    }

    float maxDelta = measureClickDelta(output, seqFreqs, kNumSteps,
                                       kSamplesPerStep, 3, kSR);
    INFO("Max envelope delta at note transitions (high res): " << maxDelta);
    REQUIRE(maxDelta < 0.02f);
}

TEST_CASE("Sequencer retrigger no clicks with envelope off + filter + saturation", "[seqclick]")
{
    // Regression: envelope disabled means no retrigger fade.
    // The gain smoother + portamento must keep transitions clean on their own.
    constexpr float kSR = 44100.0f;
    constexpr float kBPM = 140.0f;
    constexpr int kSamplesPerStep = static_cast<int>(kSR * 60.0f / kBPM);
    constexpr int kGateSamples = kSamplesPerStep * 80 / 100;

    const float kC3 = 130.81f;
    const float kEb3 = 155.56f;
    const float kG3 = 196.00f;
    float seqFreqs[] = {kC3, kG3, 0.0f, kEb3, kC3, kG3, 0.0f, kC3};
    constexpr int kNumSteps = 8;

    Oscillator osc;
    osc.prepare(kSR);
    osc.setFrequency(kC3);

    // No AdsrEnvelope — envelope is off.

    SVFilter filter;
    filter.prepare(kSR);
    filter.setCutoff(500.0f);
    // Moderate-high resonance: Q=10 → (1 - 0.707/10) * 0.9 ≈ 0.84
    filter.setResonance((1.0f - (0.707f / 10.0f)) * 0.9f);
    filter.setMode(SVFilter::Mode::Lowpass);

    // Portamento with minimum 3ms glide (matches REPL sequencer floor)
    Portamento porta;
    porta.prepare(kSR);
    porta.setTime(0.003f);
    porta.setValue(kC3);
    porta.setTarget(kC3);

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
    porta.setTarget(seqFreqs[0]);
    gainSmoother.setTarget(0.7f);
    gateOpen = true;

    for (int i = 0; i < totalSamples; ++i)
    {
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
                porta.setTarget(f);
                // Without envelope, gain smoother must ramp (not jump)
                // to avoid clicks at retrigger boundaries.
                gainSmoother.setTarget(0.7f);
                gateOpen = true;
            }
            else
            {
                gainSmoother.setTarget(0.0f);
                gateOpen = false;
            }
        }

        float gain = gainSmoother.process();
        float freq = porta.process();
        osc.setFrequency(freq);
        float sample = osc.process(1.0f);

        // Osc → Filter → Saturation (no envelope)
        sample = filter.process(sample);
        sample = Saturation::tanhDrive(sample, 2.5f);
        output[i] = sample * gain * 0.5f;
    }

    float maxDelta = measureClickDelta(output, seqFreqs, kNumSteps,
                                       kSamplesPerStep, 2, kSR);
    INFO("Max envelope delta at note transitions (no env): " << maxDelta);
    REQUIRE(maxDelta < 0.02f);
}
