#include "AudioEngine.h"
#include <cmath>

namespace ideath { namespace repl {

void AudioEngine::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    osc_.prepare(sampleRate);
    wt_.prepare(sampleRate);
    env_.prepare(sampleRate);
    filter_.reset();
    crush_.prepare(sampleRate);
    delay_.prepare(sampleRate, 2.0f); // max 2 seconds delay
    lfo_.prepare(sampleRate);
    porta_.prepare(sampleRate);
    gainSmoother_.prepare(sampleRate);
    gainSmoother_.setTime(0.005f); // 5ms fade
    gainSmoother_.setValue(0.0f);
    gainSmoother_.setTarget(0.0f);

    seq_ = {};
    seqStep_ = 0;
    seqSampleCounter_ = 0;
    seqSamplesPerStep_ = 0;
    seqGateSamples_ = 0;
    seqGateOpen_ = false;
}

void AudioEngine::applyPendingState(SharedState& shared)
{
    if (shared.paramsReady.load(std::memory_order_acquire))
    {
        VoiceParams newParams = shared.staging;
        shared.paramsReady.store(false, std::memory_order_release);

        // Wavetable: only rebuild when shape actually changes
        if (newParams.source == SourceType::Wavetable &&
            (newParams.wtShape != params_.wtShape || params_.source != SourceType::Wavetable))
        {
            switch (newParams.wtShape)
            {
                case WtShape::Square:   wt_ = Wavetable::squareTable(); break;
                case WtShape::Saw:      wt_ = Wavetable::sawTable(); break;
                case WtShape::Triangle: wt_ = Wavetable::triangleTable(); break;
                case WtShape::Sine:     wt_ = Wavetable::sineTable(); break;
            }
            wt_.prepare(sampleRate_);
        }

        // Update gain target for smooth transitions
        if (newParams.source != SourceType::None)
            gainSmoother_.setTarget(1.0f);
        else
            gainSmoother_.setTarget(0.0f);

        params_ = newParams;
    }

    if (shared.stopRequested.load(std::memory_order_acquire))
    {
        stopped_ = true;
        env_.noteOff();
        gainSmoother_.setTarget(0.0f);
        shared.stopRequested.store(false, std::memory_order_release);
    }

    // Check for note events
    int noteOn = shared.noteOnCounter.load(std::memory_order_acquire);
    if (noteOn != lastNoteOn_)
    {
        lastNoteOn_ = noteOn;
        stopped_ = false;
        baseFreq_ = params_.frequency;
        porta_.setTarget(baseFreq_);
        gainSmoother_.setTarget(1.0f);

        if (params_.envelopeEnabled)
        {
            env_.setAttack(params_.attack);
            env_.setDecay(params_.decay);
            env_.setSustain(params_.sustain);
            env_.setRelease(params_.release);
            env_.noteOn();
        }
    }

    int noteOff = shared.noteOffCounter.load(std::memory_order_acquire);
    if (noteOff != lastNoteOff_)
    {
        lastNoteOff_ = noteOff;
        env_.noteOff();
    }

    // --- Sequencer ---
    if (shared.seqReady.load(std::memory_order_acquire))
    {
        seq_ = shared.seqStaging;
        shared.seqReady.store(false, std::memory_order_release);

        if (seq_.running && seq_.numSteps > 0)
        {
            seqStep_ = 0;
            seqSampleCounter_ = 0;
            seqSamplesPerStep_ = static_cast<int>(sampleRate_ * 60.0f / seq_.bpm);
            seqGateSamples_ = seqSamplesPerStep_ * static_cast<int>(seq_.gatePercent) / 100;
            seqGateOpen_ = false;

            // Trigger first step immediately
            float freq = seq_.frequencies[0];
            if (freq > 0.0f)
            {
                params_.frequency = freq;
                stopped_ = false;
                baseFreq_ = freq;
                porta_.setTarget(freq);
                seqVelocity_ = seq_.velocities[0] > 0.0f ? seq_.velocities[0] : 1.0f;
                gainSmoother_.setTarget(seqVelocity_);

                if (params_.envelopeEnabled)
                {
                    env_.setAttack(params_.attack);
                    env_.setDecay(params_.decay);
                    env_.setSustain(params_.sustain);
                    env_.setRelease(params_.release);
                    env_.noteOn();
                }
                seqGateOpen_ = true;
            }
        }
        else
        {
            seq_.running = false;
            if (seqGateOpen_)
            {
                env_.noteOff();
                seqGateOpen_ = false;
            }
        }
    }
}

void AudioEngine::advanceSequencer()
{
    if (!seq_.running || seq_.numSteps <= 0) return;

    // Gate off at 80% of step
    if (seqGateOpen_ && seqSampleCounter_ >= seqGateSamples_)
    {
        env_.noteOff();
        seqGateOpen_ = false;
    }

    ++seqSampleCounter_;
    if (seqSampleCounter_ < seqSamplesPerStep_) return;

    // Advance to next step
    seqSampleCounter_ = 0;
    seqStep_ = (seqStep_ + 1) % seq_.numSteps;

    float freq = seq_.frequencies[seqStep_];
    if (freq > 0.0f)
    {
        params_.frequency = freq;
        stopped_ = false;
        baseFreq_ = freq;
        porta_.setTarget(freq);
        seqVelocity_ = seq_.velocities[seqStep_] > 0.0f ? seq_.velocities[seqStep_] : 1.0f;
        gainSmoother_.setTarget(seqVelocity_);

        if (params_.envelopeEnabled)
        {
            env_.setAttack(params_.attack);
            env_.setDecay(params_.decay);
            env_.setSustain(params_.sustain);
            env_.setRelease(params_.release);
            env_.noteOn();
        }
        seqGateOpen_ = true;
    }
    // freq == 0 means rest: no noteOn, gate stays closed
}

float AudioEngine::process()
{
    advanceSequencer();

    float gain = gainSmoother_.process();

    if (gain < 0.0001f && params_.source == SourceType::None)
    {
        // Fully faded out, safe to reset
        if (!delayCleared_)
        {
            delay_.reset();
            lfo_.reset();
            delayCleared_ = true;
        }
        return 0.0f;
    }
    delayCleared_ = false;

    // --- Portamento ---
    porta_.setTime(params_.portaTime);
    porta_.setTarget(params_.frequency);
    float freq = porta_.process();

    // --- LFO modulation ---
    float lfoVal = 0.0f;
    if (params_.lfoTarget != LfoTarget::Off)
    {
        lfo_.setRate(params_.lfoRate);

        switch (params_.lfoWaveform)
        {
            case LfoWaveform::Sine:          lfo_.setWaveform(LFO::Waveform::Sine); break;
            case LfoWaveform::Triangle:      lfo_.setWaveform(LFO::Waveform::Triangle); break;
            case LfoWaveform::Square:        lfo_.setWaveform(LFO::Waveform::Square); break;
            case LfoWaveform::Saw:           lfo_.setWaveform(LFO::Waveform::Saw); break;
            case LfoWaveform::SampleAndHold: lfo_.setWaveform(LFO::Waveform::SampleAndHold); break;
        }

        lfo_.setPolarity(LFO::Polarity::Bipolar);
        lfoVal = lfo_.process();

        if (params_.lfoTarget == LfoTarget::Pitch)
            freq *= std::pow(2.0f, lfoVal * params_.lfoDepth / 1200.0f); // depth in cents
    }

    // --- Source ---
    float sample = 0.0f;

    switch (params_.source)
    {
        case SourceType::Oscillator:
            osc_.setFrequency(freq);
            sample = osc_.process(params_.oscWaveform == OscWaveform::Saw ? 1.0f : 0.0f);
            break;

        case SourceType::Wavetable:
            wt_.setFrequency(freq);
            sample = wt_.process();
            break;

        case SourceType::Noise:
            sample = noise_.process();
            break;

        case SourceType::None:
            break;
    }

    // --- Envelope ---
    if (params_.envelopeEnabled)
    {
        float envVal = env_.process();
        sample *= envVal;
    }

    // --- Filter ---
    if (params_.filterType != FilterType::Off)
    {
        float filterFreq = params_.filterFreq;

        // LFO → filter modulation
        if (params_.lfoTarget == LfoTarget::Filter)
            filterFreq *= std::pow(2.0f, lfoVal * params_.lfoDepth / 1200.0f);

        // Only recompute coefficients when parameters actually change
        if (filterFreq != lastFilterFreq_ || params_.filterQ != lastFilterQ_ || params_.filterType != lastFilterType_)
        {
            switch (params_.filterType)
            {
                case FilterType::Lowpass:  filter_.setLowpass(filterFreq, params_.filterQ, sampleRate_); break;
                case FilterType::Highpass: filter_.setHighpass(filterFreq, params_.filterQ, sampleRate_); break;
                case FilterType::Bandpass: filter_.setBandpass(filterFreq, params_.filterQ, sampleRate_); break;
                default: break;
            }
            lastFilterFreq_ = filterFreq;
            lastFilterQ_ = params_.filterQ;
            lastFilterType_ = params_.filterType;
        }

        sample = filter_.process(sample);
    }

    // --- BitCrusher ---
    if (params_.crushEnabled)
    {
        crush_.setBitDepth(params_.crushBits);
        crush_.setDownsampleRate(params_.crushRate);
        sample = crush_.process(sample);
    }

    // --- Saturation ---
    if (params_.satEnabled)
        sample = Saturation::tanhDrive(sample, params_.satDrive);

    // --- Delay ---
    if (params_.delayEnabled)
    {
        delay_.setDelay(params_.delayTime);
        delay_.setFeedback(params_.delayFeedback);
        delay_.setMix(0.5f);
        sample = delay_.process(sample);
    }

    // --- LFO → Volume ---
    if (params_.lfoTarget == LfoTarget::Volume)
        sample *= 1.0f + lfoVal * params_.lfoDepth * 0.01f;

    // --- Master volume with gain smoothing ---
    sample *= params_.volume * gain;

    // --- Hard limiter: protect speakers ---
    if (sample > 1.0f) sample = 1.0f;
    else if (sample < -1.0f) sample = -1.0f;

    return sample;
}

}} // namespace ideath::repl
