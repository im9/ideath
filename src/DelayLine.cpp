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
    // Read position with linear interpolation
    float readPos = static_cast<float>(writePos_) - delaySamples_;
    if (readPos < 0.0f)
        readPos += static_cast<float>(bufferSize_);

    int i0 = static_cast<int>(readPos);
    int i1 = i0 + 1;
    if (i0 >= bufferSize_) i0 -= bufferSize_;
    if (i1 >= bufferSize_) i1 -= bufferSize_;

    float frac = readPos - std::floor(readPos);

    return buffer_[static_cast<size_t>(i0)]
         + frac * (buffer_[static_cast<size_t>(i1)] - buffer_[static_cast<size_t>(i0)]);
}

float DelayLine::process(float input)
{
    // Read from delay line first
    float wet = readDelay();

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
