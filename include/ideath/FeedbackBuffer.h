#pragma once

#include <vector>

namespace ideath {

/// Long circular-buffer looper with record, overdub, and playback modes.
/// Builds on the same circular buffer concept as DelayLine but designed
/// for looper-style operation: record a phrase, then overdub or play back.
class FeedbackBuffer
{
public:
    enum class Mode { Stopped, Recording, Overdub, Playing };

    FeedbackBuffer() = default;

    /// Allocate buffer for up to maxLengthSec seconds.
    void prepare(float sampleRate, float maxLengthSec = 10.0f);
    void reset();

    /// Set feedback amount for overdub (0 = replace, 1 = full mix). Clamped to [0, 1].
    void setFeedback(float feedback);

    /// Set dry/wet mix (0 = fully dry, 1 = fully wet). Clamped to [0, 1].
    void setMix(float mix);

    /// Start recording (resets loop length).
    void record();

    /// Stop recording and set loop length, or stop playback.
    void stop();

    /// Start playback from beginning of loop.
    void play();

    /// Start overdub (play + record layered).
    void overdub();

    /// Process one sample.
    float process(float input);

    Mode getMode() const { return mode_; }
    int getLoopLength() const { return loopLength_; }

    /// Set crossfade length in seconds (default 0.005 = 5ms).
    void setCrossfade(float seconds);

private:
    float readSample(int pos) const;

    float sampleRate_ = 44100.0f;
    std::vector<float> buffer_;
    int bufferSize_ = 0;
    int writePos_ = 0;
    int readPos_ = 0;
    int loopLength_ = 0;
    int crossfadeSamples_ = 0;
    Mode mode_ = Mode::Stopped;
    float feedback_ = 0.8f;
    float mix_ = 1.0f;
};

} // namespace ideath
