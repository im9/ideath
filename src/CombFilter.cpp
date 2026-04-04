#include <ideath/CombFilter.h>
#include <algorithm>
#include <cmath>

namespace ideath {

static constexpr float kAntiDenormal = 1e-25f;

void CombFilter::prepare(float sampleRate, float maxDelaySec)
{
    sampleRate_ = sampleRate;
    maxDelaySamples_ = std::max(1.0f, maxDelaySec * sampleRate_);
    bufferSize_ = static_cast<int>(maxDelaySamples_) + 2;
    buffer_.resize(static_cast<size_t>(bufferSize_), 0.0f);
    reset();
}

void CombFilter::reset()
{
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    writePos_ = 0;
    filterStore_ = 0.0f;
}

void CombFilter::setDelay(float delaySec)
{
    delaySamples_ = std::clamp(delaySec * sampleRate_, 1.0f, maxDelaySamples_);
}

void CombFilter::setFeedback(float feedback)
{
    feedback_ = std::clamp(feedback, -0.999f, 0.999f);
}

void CombFilter::setDamp(float damp)
{
    damp_ = std::clamp(damp, 0.0f, 1.0f);
}

void CombFilter::setMix(float mix)
{
    mix_ = std::clamp(mix, 0.0f, 1.0f);
}

float CombFilter::readDelay() const
{
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

float CombFilter::process(float input)
{
    float wet = readDelay();

    filterStore_ += (wet - filterStore_) * (1.0f - damp_);
    float feedbackSample = filterStore_;

    buffer_[static_cast<size_t>(writePos_)] = input + feedbackSample * feedback_ + kAntiDenormal;

    ++writePos_;
    if (writePos_ >= bufferSize_)
        writePos_ = 0;

    return input * (1.0f - mix_) + wet * mix_;
}

} // namespace ideath
