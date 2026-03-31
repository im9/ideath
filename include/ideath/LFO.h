#pragma once

#include <cstdint>

namespace ideath {

/// Low-frequency oscillator for modulation.
/// Supports sine, triangle, square, saw, and sample-and-hold waveforms.
/// Output can be unipolar [0, 1] or bipolar [-1, 1].
class LFO
{
public:
    enum class Waveform { Sine, Triangle, Square, Saw, SampleAndHold };
    enum class Polarity { Bipolar, Unipolar };

    LFO() = default;

    void prepare(float sampleRate);
    void reset();

    /// Set rate in Hz (typically 0.01 – 50).
    void setRate(float rateHz);
    void setWaveform(Waveform wf);
    void setPolarity(Polarity pol);

    /// Enable one-shot mode: runs one cycle then holds at end value.
    void setOneShot(bool enabled);

    /// Retrigger from phase 0 (useful for synced or one-shot use).
    void trigger();

    float process();

    float getPhase() const { return phase_; }
    bool isFinished() const { return finished_; }

private:
    float sampleRate_ = 44100.0f;
    float phase_ = 0.0f;
    float phaseInc_ = 0.0f;
    Waveform waveform_ = Waveform::Sine;
    Polarity polarity_ = Polarity::Bipolar;
    bool oneShot_ = false;
    bool finished_ = false;
    float holdValue_ = 0.0f;     // for S&H
    float prevPhase_ = 0.0f;     // to detect phase wrap for S&H
    uint32_t noiseState_ = 0x12345678u; // for S&H random
};

} // namespace ideath
