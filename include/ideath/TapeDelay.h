#pragma once

#include "Biquad.h"
#include <vector>

namespace ideath {

/// Tape-style mono delay with wow/flutter modulation, feedback tone shaping,
/// and saturated repeats.
class TapeDelay
{
public:
    TapeDelay() = default;

    void prepare(float sampleRate, float maxDelaySec = 2.0f);
    void reset();

    /// Base delay time in seconds.
    void setDelay(float delaySec);

    /// Feedback amount. Clamped to [-0.999, 0.999].
    void setFeedback(float feedback);

    /// Dry/wet mix (0 = fully dry, 1 = fully wet).
    void setMix(float mix);

    /// Wow modulation depth/rate (slow, larger drift).
    void setWowDepth(float seconds);
    void setWowRate(float rateHz);

    /// Flutter modulation depth/rate (fast, small wobble).
    void setFlutterDepth(float seconds);
    void setFlutterRate(float rateHz);

    /// Feedback path tone shaping.
    void setLowpass(float freqHz);
    void setHighpass(float freqHz);

    /// Saturation drive on repeats (>= 1.0).
    void setDrive(float drive);

    float process(float input);

    float getDelaySamples() const { return delaySamples_; }

private:
    float readDelay(float delaySamples) const;
    void updateFeedbackFilters();

    float sampleRate_ = 44100.0f;
    std::vector<float> buffer_;
    int bufferSize_ = 0;
    int writePos_ = 0;
    float maxDelaySamples_ = 0.0f;

    float delaySamples_ = 1.0f;
    float feedback_ = 0.0f;
    float mix_ = 1.0f;

    float wowDepthSamples_ = 0.0f;
    float wowPhase_ = 0.0f;
    float wowInc_ = 0.0f;

    float flutterDepthSamples_ = 0.0f;
    float flutterPhase_ = 0.0f;
    float flutterInc_ = 0.0f;

    float lowpassHz_ = 8000.0f;
    float highpassHz_ = 40.0f;
    float drive_ = 1.5f;

    Biquad highpass_;
    Biquad lowpass_;
};

} // namespace ideath
