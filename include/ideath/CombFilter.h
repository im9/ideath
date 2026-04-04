#pragma once

#include <vector>

namespace ideath {

/// Feedback comb filter with fractional delay and one-pole damping.
/// Useful for Karplus-Strong plucks, metallic resonances, and resonators.
class CombFilter
{
public:
    CombFilter() = default;

    void prepare(float sampleRate, float maxDelaySec = 0.1f);
    void reset();

    /// Delay time in seconds. Clamped to [1 sample, maxDelay].
    void setDelay(float delaySec);

    /// Feedback amount. Clamped to [-0.999, 0.999].
    void setFeedback(float feedback);

    /// Damping in the feedback path (0 = bright, 1 = dark).
    void setDamp(float damp);

    /// Dry/wet mix (0 = fully dry, 1 = fully wet).
    void setMix(float mix);

    float process(float input);

    float getDelaySamples() const { return delaySamples_; }

private:
    float readDelay() const;

    float sampleRate_ = 44100.0f;
    std::vector<float> buffer_;
    int bufferSize_ = 0;
    int writePos_ = 0;
    float maxDelaySamples_ = 0.0f;
    float delaySamples_ = 1.0f;
    float feedback_ = 0.0f;
    float damp_ = 0.0f;
    float mix_ = 1.0f;
    float filterStore_ = 0.0f;
};

} // namespace ideath
