#pragma once

#include "Oscillator.h"
#include "Wavetable.h"
#include "Noise.h"
#include "Envelope.h"
#include "Biquad.h"
#include "LFO.h"
#include "Portamento.h"
#include "BitCrusher.h"

namespace ideath {

/// A single synthesizer voice bundling source + envelope + filter + effects.
/// Designed for use in a polyphonic context (managed by Polyphony).
class Voice
{
public:
    enum class Source { Oscillator, Wavetable, Noise };

    Voice() = default;

    void prepare(float sampleRate);
    void reset();

    // --- Note control ---
    void noteOn(float freqHz, float velocity = 1.0f);
    void noteOff();

    /// True while the envelope is still producing output (including release).
    bool isActive() const;

    /// The MIDI note frequency this voice was triggered with.
    float getFrequency() const { return noteFreq_; }

    // --- Source selection ---
    void setSource(Source src);
    Source getSource() const { return source_; }

    /// Oscillator waveform morph: 0.0 = square, 1.0 = saw.
    void setOscWaveform(float wf);

    /// Load a wavetable (4-bit values 0-15).
    void setWavetable(const float* data, int size);
    /// Load a normalized wavetable ([-1, 1]).
    void setWavetableNormalized(const float* data, int size);

    // --- Envelope (ADSR) ---
    void setAttack(float seconds);
    void setDecay(float seconds);
    void setSustain(float level);
    void setRelease(float seconds);

    // --- Filter ---
    enum class FilterType { Off, Lowpass, Highpass, Bandpass };
    void setFilter(FilterType type, float freqHz, float q);

    // --- LFO → pitch modulation ---
    void setLfoRate(float rateHz);
    void setLfoPitchDepth(float semitones);

    // --- Portamento ---
    void setPortamento(float timeSec);

    // --- BitCrusher ---
    void setBitDepth(int bits);
    void setDownsampleRate(float rateHz);

    /// Process one sample through the full signal chain.
    float process();

private:
    float sampleRate_ = 44100.0f;
    Source source_ = Source::Oscillator;
    float noteFreq_ = 440.0f;
    float velocity_ = 1.0f;
    float oscWaveform_ = 1.0f; // saw

    // Signal chain components
    Oscillator osc_;
    Wavetable wt_;
    Noise noise_;
    AdsrEnvelope env_;
    Biquad filter_;
    LFO lfo_;
    Portamento porta_;
    BitCrusher crush_;

    // Filter state
    FilterType filterType_ = FilterType::Off;
    float filterFreq_ = 1000.0f;
    float filterQ_ = 0.707f;

    // LFO modulation
    float lfoPitchDepth_ = 0.0f; // in semitones
};

} // namespace ideath
