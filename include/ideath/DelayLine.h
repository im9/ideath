#pragma once

#include <vector>

namespace ideath {

/// Circular-buffer delay line with linear interpolation.
/// Foundation for delay, chorus, flanger, and comb filter effects.
class DelayLine
{
public:
    DelayLine() = default;

    /// Allocate buffer for up to maxDelaySec seconds.
    void prepare(float sampleRate, float maxDelaySec = 1.0f);
    void reset();

    /// Set delay time in seconds (clamped to [0, maxDelay]).
    void setDelay(float delaySec);

    /// Set feedback amount (clamped to [-1, 1]).
    void setFeedback(float feedback);

    /// Set dry/wet mix (0 = fully dry, 1 = fully wet).
    void setMix(float mix);

    /// Process one sample. Returns mixed output.
    float process(float input);

    /// Read from the delay line at the current delay time (no write).
    float readDelay() const;

    float getDelaySamples() const { return delaySamples_; }

private:
    float sampleRate_ = 44100.0f;
    std::vector<float> buffer_;
    int writePos_ = 0;
    int bufferSize_ = 0;
    float delaySamples_ = 0.0f;
    float maxDelaySamples_ = 0.0f;
    float feedback_ = 0.0f;
    float mix_ = 1.0f;
};

} // namespace ideath
