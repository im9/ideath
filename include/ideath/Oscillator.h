#pragma once

namespace ideath {

/// Band-limited-ish phase-accumulator oscillator.
/// Supports saw, square, and continuous morph between them.
class Oscillator
{
public:
    Oscillator() = default;

    void prepare(float sampleRate);
    void reset();

    void setFrequency(float freqHz);

    /// Returns next sample. waveform: 0.0 = square, 1.0 = saw.
    float process(float waveform = 1.0f);

    float getPhase() const { return phase_; }

private:
    float sampleRate_ = 44100.0f;
    float phase_ = 0.0f;
    float phaseInc_ = 0.0f;
};

} // namespace ideath
