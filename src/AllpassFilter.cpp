#include <ideath/AllpassFilter.h>
#include <algorithm>

namespace ideath {

// DC offset to prevent denormals accumulating in the feedback buffer.
// Same value used across the library (see CLAUDE.md, commit eede258).
static constexpr float kAntiDenormal = 1e-25f;

AllpassFilter::AllpassFilter()
{
    prepare(44100.0f, 0.1f);
}

void AllpassFilter::prepare(float sampleRate, float maxDelaySec)
{
    sampleRate_       = sampleRate;
    maxDelaySamples_  = std::max(1, static_cast<int>(maxDelaySec * sampleRate));
    bufferSize_       = maxDelaySamples_;
    buffer_.assign(static_cast<size_t>(bufferSize_), 0.0f);
    writePos_         = 0;
    delaySamples_     = std::min(delaySamples_, maxDelaySamples_);
    if (delaySamples_ < 1) delaySamples_ = 1;
}

void AllpassFilter::reset()
{
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    writePos_ = 0;
}

void AllpassFilter::setDelay(float delaySec)
{
    const int requested = static_cast<int>(delaySec * sampleRate_);
    delaySamples_ = std::clamp(requested, 1, maxDelaySamples_);
}

void AllpassFilter::setFeedback(float g)
{
    g_ = std::clamp(g, -0.99f, 0.99f);
}

float AllpassFilter::process(float input)
{
    // Schroeder allpass, direct-form II:
    //     w[n]    = x[n] + g w[n-N]
    //     y[n]    = -g w[n] + w[n-N]
    //     buf[n]  = w[n]
    // Reading w[n-N] from writePos - N (with circular wrap).
    int readPos = writePos_ - delaySamples_;
    if (readPos < 0) readPos += bufferSize_;

    const float wDelayed = buffer_[static_cast<size_t>(readPos)];
    const float wNow     = input + g_ * wDelayed;
    const float output   = -g_ * wNow + wDelayed;

    buffer_[static_cast<size_t>(writePos_)] = wNow + kAntiDenormal;

    if (++writePos_ >= bufferSize_)
        writePos_ = 0;

    return output;
}

} // namespace ideath
