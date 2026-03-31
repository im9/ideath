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
}

void AudioEngine::applyPendingState(SharedState& shared)
{
    if (shared.paramsReady.load(std::memory_order_acquire))
    {
        params_ = shared.staging;
        shared.paramsReady.store(false, std::memory_order_release);
    }

    if (shared.stopRequested.load(std::memory_order_acquire))
    {
        stopped_ = true;
        env_.noteOff();
        delay_.reset();
        lfo_.reset();
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
}

float AudioEngine::process()
{
    if (stopped_ && params_.source == SourceType::None)
        return 0.0f;

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
        {
            // Rebuild wavetable if shape changed (cheap for small tables)
            switch (params_.wtShape)
            {
                case WtShape::Square:   wt_ = Wavetable::squareTable(); break;
                case WtShape::Saw:      wt_ = Wavetable::sawTable(); break;
                case WtShape::Triangle: wt_ = Wavetable::triangleTable(); break;
                case WtShape::Sine:     wt_ = Wavetable::sineTable(); break;
            }
            wt_.prepare(sampleRate_);
            wt_.setFrequency(freq);
            sample = wt_.process();
            break;
        }

        case SourceType::Noise:
            sample = noise_.process();
            break;

        case SourceType::None:
            return 0.0f;
    }

    // --- Envelope ---
    if (params_.envelopeEnabled)
    {
        float envVal = env_.process();
        sample *= envVal;

        // If envelope finished and in envelope mode, go silent
        if (!env_.isActive() && lastNoteOff_ > 0)
            sample = 0.0f;
    }

    // --- Filter ---
    if (params_.filterType != FilterType::Off)
    {
        float filterFreq = params_.filterFreq;

        // LFO → filter modulation
        if (params_.lfoTarget == LfoTarget::Filter)
            filterFreq *= std::pow(2.0f, lfoVal * params_.lfoDepth / 1200.0f);

        switch (params_.filterType)
        {
            case FilterType::Lowpass:  filter_.setLowpass(sampleRate_, filterFreq, params_.filterQ); break;
            case FilterType::Highpass: filter_.setHighpass(sampleRate_, filterFreq, params_.filterQ); break;
            case FilterType::Bandpass: filter_.setBandpass(sampleRate_, filterFreq, params_.filterQ); break;
            default: break;
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
        sample *= 1.0f + lfoVal * params_.lfoDepth * 0.01f; // depth as percentage

    // --- Master volume ---
    sample *= params_.volume;

    return sample;
}

}} // namespace ideath::repl
