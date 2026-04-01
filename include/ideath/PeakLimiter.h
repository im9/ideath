#pragma once

#include <vector>

namespace ideath {

/// Lookahead brickwall peak limiter.
/// Guarantees output never exceeds threshold.
/// Uses lookahead delay to anticipate peaks and smooth gain reduction.
class PeakLimiter
{
public:
    PeakLimiter();

    /// Initialize with sample rate, allocate buffers, call reset().
    void prepare(float sampleRate);

    /// Clear all buffers and reset to initial state.
    void reset();

    /// Threshold in dB (e.g., 0.0 = unity, -3.0 = 3dB below).
    void setThreshold(float dB);

    /// Release time in seconds. How fast gain recovers after peak.
    void setRelease(float seconds);

    /// Lookahead time in seconds (0.001–0.005 typical).
    void setLookahead(float seconds);

    /// Process one sample. Returns limited output.
    float process(float input);

    /// Current gain reduction in dB (for metering, always <= 0).
    float getGainReductionDb() const;

private:
    float sampleRate_ = 44100.0f;
    float thresholdLinear_ = 1.0f;
    float releaseCoeff_ = 0.0f;
    int lookaheadSamples_ = 0;

    // Lookahead delay buffer
    std::vector<float> delayBuf_;
    int delayWriteIdx_ = 0;

    // Envelope follower state
    float envelope_ = 0.0f;

    // Current gain (linear, 0–1)
    float currentGain_ = 1.0f;
};

} // namespace ideath
