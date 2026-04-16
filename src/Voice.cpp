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
    filter_.prepare(sampleRate);
    lfo_.prepare(sampleRate);
    porta_.prepare(sampleRate);
    crush_.prepare(sampleRate);
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

    if (type == FilterType::Off)
        return;

    filter_.setCutoff(freqHz);

    // Map Biquad-style Q to SVFilter resonance.  Reference: REPL
    // AudioEngine.cpp (filter block).  q = 0.707 → res = 0 (no
    // resonance), q → ∞ → res = 0.9 (cap to avoid ringing).
    const float res = (1.0f - (0.707f / std::max(q, 0.707f))) * 0.9f;
    filter_.setResonance(res);

    switch (type)
    {
        case FilterType::Off: break; // unreachable (early-out above)
        case FilterType::Lowpass:  filter_.setMode(SVFilter::Mode::Lowpass);  break;
        case FilterType::Highpass: filter_.setMode(SVFilter::Mode::Highpass); break;
        case FilterType::Bandpass: filter_.setMode(SVFilter::Mode::Bandpass); break;
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

    // Standard subtractive routing: Osc → Filter → Envelope (VCO → VCF → VCA).
    // The filter must run *before* the envelope so the AdsrEnvelope's
    // ~1ms retrigger fade can mask any filter state transients carrying over
    // from the previous note.  This was fixed in the REPL audio engine in
    // commit 3b939e7 ("eliminate sequencer retrigger clicks with resonant
    // filter + saturation") but Voice.cpp was missed at the time, so any
    // plugin building on Voice (rather than the REPL) was still hearing the
    // exact retrigger click that the commit claimed to fix.

    // Filter
    if (filterType_ != FilterType::Off)
        sample = filter_.process(sample);

    // Envelope (VCA stage)
    float envVal = env_.process();
    sample *= envVal * velocity_;

    // BitCrusher (passthrough if default settings)
    sample = crush_.process(sample);

    return sample;
}

} // namespace ideath
