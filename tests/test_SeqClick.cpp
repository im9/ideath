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
#include <ideath/Voice.h>

using namespace ideath;

// Threshold derivations used throughout this file
// -----------------------------------------------
// These tests are REGRESSION tests, not behavioural specs — each one
// exists because a specific class of click bug shipped in REPL/Voice
// code once.  Threshold values therefore have to be chosen so a real
// click regression fails loudly while the click-free reference chain
// stays comfortably below the alarm line.
//
// (A) measureClickDelta()'s envelope follower
//
//   envAttack  = 1 − exp(−5 / (0.001 · sr))   ≈ 0.1072 @ 44.1 kHz
//   envRelease = 1 − exp(−5 / (0.010 · sr))   ≈ 0.0113 @ 44.1 kHz
//
//   On a rising boundary the single-sample envelope step is
//       Δ_env = envAttack · (|out_new| − |out_old|)
//             ≤ 0.1072 · Δ_|out|
//   so the follower converts an audible-magnitude click into an
//   envelope delta that is ~10× smaller than the raw amplitude jump.
//   This is the knob we then test against a threshold.
//
// (B) What a "click" and a "clean transition" look like through (A)
//
//   Hard click: output |jumps| by 0.7 at a note boundary with no
//     smoothing.  Δ_env ≈ 0.1072 · 0.7 · 0.5 (the · 0.5 is the mix gain)
//                  ≈ 0.0375.
//   Clean ramp: the gain smoother has timeSec = 5 ms, so coef
//     = 1 − exp(−5 / (0.005 · 44100)) ≈ 0.0224.  Per-sample gain step
//     ≤ 0.0224 · v_target.  For v_target = 0.7, saw = ±1, mix 0.5:
//     per-sample output delta ≤ 0.0224 · 0.7 · 0.5 ≈ 0.00784.
//     Δ_env ≤ 0.1072 · 0.00784 ≈ 8.4e−4.
//
//   So a correctly ramped transition lives at ~1e−3, a hard click at
//   ~4e−2.  Threshold 0.02 sits in the valley between them (20× above
//   "clean", 2× below "click") — strict enough to catch any regression
//   that reintroduces a hard boundary but loose enough to tolerate the
//   small ringing contribution of a resonant filter being retriggered
//   (SVFilter at resonance ≈ 0.86 rings at amplitude ≤ 1/Q ≈ 0.07 per
//   cycle, envelope-followed to Δ_env ≤ 0.107 · 0.07 · 0.5 ≈ 3.7e−3
//   on the worst transient, well under 0.02).
//
// (C) Voice-vs-offline RMS diff (signal-chain-order test)
//
//   When Voice's chain order is correct (Osc → Filter → Env, matching
//   the offline reference) the two paths execute bit-for-bit identical
//   ops on identical state; the only measurable difference is Voice's
//   default BitCrusher at 32-bit, which introduces ≤ 6e−8 per-sample
//   quantisation noise.  Accumulated over 4096 samples the RMS diff
//   stays at ≈ 6e−8 ≪ 0.01.
//
//   When the chain order is wrong (old Voice: Env → Filter) the
//   resonant LP (Q=4) sees an envelope-shaped input on attack, so its
//   frequency response and phase evolution diverge from the reference.
//   Empirically that regression gives RMS diff ≈ 0.1.  Threshold 0.01
//   therefore sits 10× below the "wrong" level and 10^5 above the
//   "right" noise floor — a comfortable separation.

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

TEST_CASE("Voice signal chain order matches REPL reference", "[seqclick][voice]")
{
    // Regression for the Voice signal chain order bug.  Voice.cpp used to
    // run the envelope BEFORE the filter; commit 3b939e7 fixed this in
    // the REPL audio engine but missed Voice.cpp itself.  We can't easily
    // measure clicks here because Voice still uses Biquad (not SVFilter
    // like the REPL), so absolute click magnitude is not comparable.
    //
    // Instead we directly compare Voice's output against an offline
    // replica of the REPL's reference chain (Osc → Filter → Envelope).
    // If Voice's order is wrong, the two outputs diverge wildly; if it's
    // correct, they track within rounding (and the small differences
    // attributable to Biquad vs SVFilter).
    //
    // See header comment (C) for the threshold 0.01 derivation: 10×
    // below the observed wrong-order divergence of ≈ 0.1 and 10^5 above
    // the correct-order BitCrusher-quantisation noise floor of ≈ 6e−8.
    constexpr float kSR = 44100.0f;
    constexpr int kN   = 4096;

    Voice voice;
    voice.prepare(kSR);
    voice.setSource(Voice::Source::Oscillator);
    voice.setOscWaveform(1.0f);
    voice.setAttack(0.005f);
    voice.setDecay(0.05f);
    voice.setSustain(0.5f);
    voice.setRelease(0.1f);
    voice.setFilter(Voice::FilterType::Lowpass, 1000.0f, 4.0f);

    // Offline reference using the same Biquad mapping Voice uses internally
    Oscillator refOsc;
    refOsc.prepare(kSR);
    refOsc.setFrequency(220.0f);
    AdsrEnvelope refEnv;
    refEnv.prepare(kSR);
    refEnv.setAttack(0.005f);
    refEnv.setDecay(0.05f);
    refEnv.setSustain(0.5f);
    refEnv.setRelease(0.1f);
    Biquad refFilter;
    refFilter.setLowpass(1000.0f, 4.0f, kSR);

    voice.noteOn(220.0f, 1.0f);
    refEnv.noteOn();

    double sumSqDiff = 0.0;
    for (int i = 0; i < kN; ++i)
    {
        // Offline: Osc → Filter → Envelope (the documented order)
        float src = refOsc.process(1.0f);
        src = refFilter.process(src);
        src *= refEnv.process();

        float v = voice.process();
        const double d = static_cast<double>(v - src);
        sumSqDiff += d * d;
    }

    const double rmsDiff = std::sqrt(sumSqDiff / static_cast<double>(kN));
    INFO("Voice vs offline-Osc→Filter→Env RMS diff: " << rmsDiff);
    REQUIRE(rmsDiff < 0.01);
}

TEST_CASE("Sequencer note transitions have no clicks (saw, no envelope)", "[seqclick]")
{
    // Minimum-primitive sequencer: raw saw gated only by a 5 ms
    // portamento-shaped gain.  See header comment (B): with a 5 ms
    // smoother the per-sample gain step is ≤ 0.0224 × v_target,
    // so Δ_env stays ≲ 1e−3 on clean transitions.  Threshold 0.02
    // rejects any regression that removes the smoother (hard 0→v
    // step → Δ_env ≈ 0.107 · 0.7 · 0.5 ≈ 0.037, fails the test).
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
    // Acid bass configuration: saw → resonant LP (Q ≈ 8) → envelope
    // → tanh drive.  The filter ringing is the new variable here —
    // SVFilter at the test's resonance = 0.82 rings at amplitude
    // ≲ 1/Q ≈ 0.125 per cycle on a retrigger transient.  Envelope-
    // followed: Δ_env ≲ 0.107 · 0.125 · 0.5 ≈ 6.7e−3 in the worst
    // sample, well under the 0.02 threshold.  See header comment (B).
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
    // Stress test: high resonance (Q=15 → SVFilter resonance ≈ 0.86),
    // short notes (3 ms attack, 30 ms release), and wide interval jumps
    // (C3 → G4 → C5 covers 2+ octaves).  Fast retrigger at high Q is
    // the exact regime the AdsrEnvelope retrigger-fade was added for
    // (~1 ms fade to mask filter-state transients).
    //
    // Worst-case filter ring on a retrigger with this Q: amplitude
    // ≲ 1/Q ≈ 0.067, then tanh-compressed at drive 3.  Envelope-
    // followed: Δ_env ≲ 0.107 · 0.067 · 0.5 ≈ 3.6e−3 per sample.
    // Threshold 0.02 ≈ 5× that — gives margin for the 3 cycles the
    // test runs and for the occasional aligned-phase peak that spikes
    // the envelope delta above the steady-state floor.
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
    // Regression: envelope disabled means the 1 ms AdsrEnvelope
    // retrigger-fade is NOT available.  The 5 ms gain smoother plus a
    // 3 ms portamento on the oscillator frequency must keep transitions
    // clean on their own — otherwise a plugin using SVFilter + drive
    // without a VCA stage would click on every note.
    //
    // Same threshold derivation as the "acid" case (SVFilter Q ≈ 10
    // ringing at amplitude ≲ 0.1 → Δ_env ≲ 5e−3), plus the gain
    // smoother's own Δ_env ≲ 1e−3 (see header comment B).  Threshold
    // 0.02 gives 3–4× margin over the predicted maxima.
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
