#pragma once

#include <ideath/Oscillator.h>
#include <vector>

namespace ideath {

/// Stacked detuned oscillators for unison/supersaw effects.
/// Spreads N voices symmetrically around the center frequency.
class UnisonOscillator
{
public:
    static constexpr int kMaxVoices = 16;

    UnisonOscillator() = default;

    void prepare(float sampleRate);
    void reset();

    /// Set number of unison voices (clamped to [1, kMaxVoices]).
    void setVoiceCount(int count);

    /// Set center frequency in Hz.
    void setFrequency(float freqHz);

    /// Set detune spread in cents (total spread, symmetric around center).
    void setDetune(float cents);

    /// Process one sample. waveform: 0.0 = square, 1.0 = saw.
    /// Returns the mixed (gain-compensated) output.
    float process(float waveform = 1.0f);

    int getVoiceCount() const { return voiceCount_; }

private:
    void updateFrequencies();

    float sampleRate_ = 44100.0f;
    float frequency_ = 440.0f;
    float detuneCents_ = 10.0f;
    int voiceCount_ = 1;
    Oscillator voices_[kMaxVoices];
};

} // namespace ideath
