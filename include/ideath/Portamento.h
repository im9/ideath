#pragma once

namespace ideath {

/// Smooth pitch/value glide using exponential approach.
/// Useful for portamento, parameter smoothing, etc.
class Portamento
{
public:
    Portamento() = default;

    void prepare(float sampleRate);
    void reset();

    /// Set glide time in seconds. 0 = instant (no glide).
    void setTime(float timeSec);

    /// Set the target value to glide towards.
    void setTarget(float target);

    /// Jump immediately to a value (no glide).
    void setValue(float value);

    float process();

    float getValue() const { return current_; }
    float getTarget() const { return target_; }

private:
    float sampleRate_ = 44100.0f;
    float current_ = 0.0f;
    float target_ = 0.0f;
    float coef_ = 1.0f; // 1.0 = instant
};

} // namespace ideath
