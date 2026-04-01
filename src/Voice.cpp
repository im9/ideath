#include "ideath/Voice.h"
#include "ideath/Saturation.h"
#include <cmath>

namespace ideath {

void Voice::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    osc_.prepare(sampleRate);
    wt_ = Wavetable::sawTable();
    wt_.prepare(sampleRate);
    env_.prepare(sampleRate);
    lfo_.prepare(sampleRate);
    porta_.prepare(sampleRate);
    crush_.prepare(sampleRate);
    filter_.reset();
    reset();
}

void Voice::reset()
{
    osc_.reset();
    wt_.reset();
    noise_.reset();
    env_.reset();
    lfo_.reset();
    porta_.reset();
    crush_.reset();
    filter_.reset();
    velocity_ = 1.0f;
}

void Voice::noteOn(float freqHz, float velocity)
{
    noteFreq_ = freqHz;
    velocity_ = velocity;
    porta_.setTarget(freqHz);
    env_.noteOn();
    lfo_.trigger();
}

void Voice::noteOff()
{
    env_.noteOff();
}

bool Voice::isActive() const
{
    return env_.isActive();
}

void Voice::setSource(Source src)
{
    source_ = src;
}

void Voice::setOscWaveform(float wf)
{
    oscWaveform_ = wf;
}

void Voice::setWavetable(const float* data, int size)
{
    wt_.setTable(data, size);
}

void Voice::setWavetableNormalized(const float* data, int size)
{
    wt_.setTableNormalized(data, size);
}

void Voice::setAttack(float seconds)
{
    env_.setAttack(seconds);
}

void Voice::setDecay(float seconds)
{
    env_.setDecay(seconds);
}

void Voice::setSustain(float level)
{
    env_.setSustain(level);
}

void Voice::setRelease(float seconds)
{
    env_.setRelease(seconds);
}

void Voice::setFilter(FilterType type, float freqHz, float q)
{
    filterType_ = type;
    filterFreq_ = freqHz;
    filterQ_ = q;

    switch (type)
    {
        case FilterType::Off:
            break;
        case FilterType::Lowpass:
            filter_.setLowpass(freqHz, q, sampleRate_);
            break;
        case FilterType::Highpass:
            filter_.setHighpass(freqHz, q, sampleRate_);
            break;
        case FilterType::Bandpass:
            filter_.setBandpass(freqHz, q, sampleRate_);
            break;
    }
}

void Voice::setLfoRate(float rateHz)
{
    lfo_.setRate(rateHz);
}

void Voice::setLfoPitchDepth(float semitones)
{
    lfoPitchDepth_ = semitones;
}

void Voice::setPortamento(float timeSec)
{
    porta_.setTime(timeSec);
}

void Voice::setBitDepth(int bits)
{
    crush_.setBitDepth(bits);
}

void Voice::setDownsampleRate(float rateHz)
{
    crush_.setDownsampleRate(rateHz);
}

float Voice::process()
{
    if (!env_.isActive())
        return 0.0f;

    // Portamento: smooth frequency transitions
    float freq = porta_.process();

    // LFO pitch modulation
    if (lfoPitchDepth_ > 0.0f)
    {
        float lfoVal = lfo_.process();
        float modSemitones = lfoVal * lfoPitchDepth_;
        freq *= std::pow(2.0f, modSemitones / 12.0f);
    }

    // Source
    float sample = 0.0f;
    switch (source_)
    {
        case Source::Oscillator:
            osc_.setFrequency(freq);
            sample = osc_.process(oscWaveform_);
            break;
        case Source::Wavetable:
            wt_.setFrequency(freq);
            sample = wt_.process();
            break;
        case Source::Noise:
            sample = noise_.process();
            break;
    }

    // Envelope
    float envVal = env_.process();
    sample *= envVal * velocity_;

    // Filter
    if (filterType_ != FilterType::Off)
        sample = filter_.process(sample);

    // BitCrusher (passthrough if default settings)
    sample = crush_.process(sample);

    return sample;
}

} // namespace ideath
