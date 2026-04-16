#include <ideath/DelayLine.h>
#include <cmath>
#include <algorithm>

namespace ideath {

void DelayLine::prepare(float sampleRate, float maxDelaySec)
{
    sampleRate_ = sampleRate;
    maxDelaySamples_ = maxDelaySec * sampleRate;
    bufferSize_ = static_cast<int>(maxDelaySamples_) + 2; // +2 for interpolation headroom
    buffer_.resize(static_cast<size_t>(bufferSize_), 0.0f);
    reset();
}

void DelayLine::reset()
{
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    writePos_ = 0;
}

void DelayLine::setDelay(float delaySec)
{
    delaySamples_ = std::clamp(delaySec * sampleRate_, 0.0f, maxDelaySamples_);
}

void DelayLine::setFeedback(float feedback)
{
    feedback_ = std::clamp(feedback, -0.999f, 0.999f);
}

void DelayLine::setMix(float mix)
{
    mix_ = std::clamp(mix, 0.0f, 1.0f);
}

float DelayLine::readDelay() const
{
    // Decompose delaySamples into integer index and sub-sample fraction.
    // Keeping the fraction at its native small magnitude avoids the float
    // ULP loss that would occur if we computed (writePos - delaySamples)
    // and wrapped by adding bufferSize (~2^12 scale ULP ≈ 5e-4 would mask
    // a ~0.5 fractional offset).
    const int delayInt = static_cast<int>(delaySamples_);
    const float frac   = delaySamples_ - static_cast<float>(delayInt);

    int idxRecent = writePos_ - delayInt;
    if (idxRecent < 0) idxRecent += bufferSize_;
    int idxOlder = idxRecent - 1;
    if (idxOlder < 0) idxOlder += bufferSize_;

    // Linear interp at position delayInt + frac samples ago:
    //   wet = (1 − frac) · buf[delayInt samples ago] + frac · buf[delayInt+1 samples ago]
    const float a = buffer_[static_cast<size_t>(idxRecent)];
    const float b = buffer_[static_cast<size_t>(idxOlder)];
    return a + frac * (b - a);
}

float DelayLine::process(float input)
{
    // Read from delay line first. When the delay is below one sample, the
    // circular-buffer read would pick up the stale value about to be
    // overwritten (equivalent to a full-buffer-size delay), so treat that
    // case as a bypass — wet mirrors the live input.
    float wet = (delaySamples_ < 1.0f) ? input : readDelay();

    // Write input + feedback into buffer (DC offset prevents denormals in feedback loop)
    buffer_[static_cast<size_t>(writePos_)] = input + wet * feedback_ + 1e-25f;

    // Advance write position
    ++writePos_;
    if (writePos_ >= bufferSize_)
        writePos_ = 0;

    // Mix dry/wet
    return input * (1.0f - mix_) + wet * mix_;
}

} // namespace ideath
