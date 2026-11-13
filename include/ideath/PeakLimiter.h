#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace ideath {

/// Lookahead brickwall peak limiter.
///
/// The audio is delayed by N − 1 samples (N = lookahead samples).  Gain
/// computer, per input sample:
///   1. target g_t = min(1, threshold / |x|)  (stereo: |x| = max(|L|, |R|));
///   2. running minimum of g_t over the last N samples (min-hold);
///   3. running mean of that over N samples — a peak entering at p pulls
///      the gain down linearly over N samples and reaches its target
///      exactly at p + N − 1, when the peak leaves the delay, so there is
///      no single-sample gain step (at most (1 − g)/N per sample);
///   4. one-pole release upward (attack follows step 3 directly);
///   5. min with the target of the sample leaving the delay, at the current
///      threshold, which makes the ceiling exact — also right after the
///      threshold is lowered: |out| ≤ threshold (+ 1 ulp of rounding).
///      (A lowered threshold acts on samples already in the window at that
///      ceiling, so the first ≤ N − 1 samples after the change can step.)
/// Steps 2–3 cost O(1) per sample (monotonic deque + running sum, the sum
/// rebuilt exactly once per N samples so it cannot drift).
///
/// processStereo() links both channels: one gain from the louder side,
/// applied to both, so the image does not shift when one side limits.  Use
/// either process() or processStereo() on one instance, not both.
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

    /// Lookahead time in seconds (0.001–0.005 typical), clamped to
    /// [0, 10 ms].  Also the attack ramp length.  Changing the length
    /// reallocates and clears the delay (not real-time safe).
    void setLookahead(float seconds);

    /// Process one sample. Returns limited output.
    float process(float input);

    /// Process one stereo frame with one linked gain.  Returns {L, R}.
    std::pair<float, float> processStereo(float left, float right);

    /// Current gain reduction in dB (for metering, always <= 0).
    float getGainReductionDb() const;

private:
    /// Steps 1–5 above for one input peak magnitude; advances the shared
    /// write index.  Returns the gain for the sample leaving the delay.
    float computeGain(float peak);
    void clearState();

    float sampleRate_ = 44100.0f;
    float thresholdLinear_ = 1.0f;
    float releaseCoeff_ = 0.0f;
    int lookaheadSamples_ = 0;
    double invLookahead_ = 1.0;

    // Lookahead delays (audio and the per-sample peak), N samples each.
    std::vector<float> delayBuf_;
    std::vector<float> delayBufR_;
    std::vector<float> peakDelay_;
    int delayWriteIdx_ = 0;

    // Min-hold: monotonic deque (ring of N) of (sample index, target).
    std::vector<float> dequeVal_;
    std::vector<uint32_t> dequePos_;
    int dequeHead_ = 0;
    int dequeTail_ = 0;
    int dequeCount_ = 0;
    uint32_t sampleIndex_ = 0; // wraps; ages are unsigned differences

    // Running mean of the min-hold over N samples.
    std::vector<float> holdBuf_;
    double holdSum_ = 0.0;

    // Release-smoothed gain (state) and the gain last applied (metering).
    float envelopeGain_ = 1.0f;
    float currentGain_ = 1.0f;
};

} // namespace ideath
