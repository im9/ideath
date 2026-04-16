#include <ideath/FeedbackBuffer.h>
#include <algorithm>
#include <cmath>

namespace ideath {

void FeedbackBuffer::prepare(float sampleRate, float maxLengthSec)
{
    sampleRate_ = sampleRate;
    bufferSize_ = static_cast<int>(maxLengthSec * sampleRate) + 1;
    buffer_.resize(static_cast<size_t>(bufferSize_), 0.0f);
    crossfadeSamples_ = static_cast<int>(0.005f * sampleRate); // 5ms default
    reset();
}

void FeedbackBuffer::reset()
{
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    writePos_ = 0;
    readPos_ = 0.0;
    loopLength_ = 0;
    mode_ = Mode::Stopped;
}

void FeedbackBuffer::setFeedback(float feedback)
{
    feedback_ = std::clamp(feedback, 0.0f, 1.0f);
}

void FeedbackBuffer::setMix(float mix)
{
    mix_ = std::clamp(mix, 0.0f, 1.0f);
}

void FeedbackBuffer::setSpeed(float speed)
{
    speed_ = std::clamp(speed, -4.0f, 4.0f);
}

void FeedbackBuffer::setCrossfade(float seconds)
{
    crossfadeSamples_ = std::max(0, static_cast<int>(seconds * sampleRate_));
}

void FeedbackBuffer::record()
{
    writePos_ = 0;
    readPos_ = 0.0;
    loopLength_ = 0;
    mode_ = Mode::Recording;
}

void FeedbackBuffer::stop()
{
    if (mode_ == Mode::Recording)
        loopLength_ = writePos_;
    mode_ = Mode::Stopped;
    readPos_ = 0.0;
}

void FeedbackBuffer::play()
{
    if (loopLength_ > 0)
    {
        // For negative speed, start at end of playable region
        if (speed_ < 0.0f)
            readPos_ = static_cast<double>(effectiveLength() - 1);
        else
            readPos_ = 0.0;
        mode_ = Mode::Playing;
    }
}

void FeedbackBuffer::overdub()
{
    if (loopLength_ > 0)
    {
        if (speed_ < 0.0f)
            readPos_ = static_cast<double>(effectiveLength() - 1);
        else
            readPos_ = 0.0;
        mode_ = Mode::Overdub;
    }
}

int FeedbackBuffer::effectiveLength() const
{
    // When crossfade is active, the playback wraps at loopLength_ − crossfadeSamples_.
    // The final cf samples of the recorded buffer are not played directly;
    // they are reused as the "tail overlap" blended into the first cf samples
    // of the playback region (see readSample). This makes the wrap point
    // (pos = effectiveLength − 1 → pos = 0) seamless regardless of signal
    // shape, because buf[effLen − 1] and the pos=0 blend both converge to
    // the same audio neighborhood.
    if (crossfadeSamples_ <= 0 || loopLength_ <= crossfadeSamples_ * 2)
        return loopLength_;
    return loopLength_ - crossfadeSamples_;
}

float FeedbackBuffer::readSample(int pos) const
{
    if (crossfadeSamples_ <= 0 || loopLength_ <= crossfadeSamples_ * 2)
        return buffer_[static_cast<size_t>(pos)];

    // Blend head [0, cf) with tail [loopLength − cf, loopLength).
    // fade = pos / cf: at pos=0 output equals tail (buf[loopLength − cf]);
    // at pos=cf−1 output ≈ head (buf[cf−1]). In the middle region
    // [cf, effectiveLength−1] the signal passes through unmodified.
    // Seamless wrap: pos=effectiveLength−1 = loopLength − cf − 1 plays
    // buf[loopLength − cf − 1]; pos=0 plays buf[loopLength − cf].
    // For any smooth recorded signal these two samples are adjacent, so
    // the per-sample step at the wrap is bounded by the signal's own slope.
    if (pos < crossfadeSamples_)
    {
        const float fade = static_cast<float>(pos) / static_cast<float>(crossfadeSamples_);
        const float head = buffer_[static_cast<size_t>(pos)];
        const float tail = buffer_[static_cast<size_t>(loopLength_ - crossfadeSamples_ + pos)];
        return head * fade + tail * (1.0f - fade);
    }
    return buffer_[static_cast<size_t>(pos)];
}

float FeedbackBuffer::readInterpolated(double pos) const
{
    const int effLen = effectiveLength();
    const double len = static_cast<double>(effLen);

    // Wrap into [0, effectiveLength)
    pos = std::fmod(pos, len);
    if (pos < 0.0)
        pos += len;

    int i0 = static_cast<int>(pos);
    int i1 = i0 + 1;
    if (i1 >= effLen)
        i1 = 0;

    const float frac = static_cast<float>(pos - std::floor(pos));
    const float s0 = readSample(i0);
    const float s1 = readSample(i1);
    return s0 + frac * (s1 - s0);
}

float FeedbackBuffer::process(float input)
{
    float output = 0.0f;

    switch (mode_)
    {
    case Mode::Stopped:
        return input;

    case Mode::Recording:
        buffer_[static_cast<size_t>(writePos_)] = input;
        ++writePos_;
        if (writePos_ >= bufferSize_)
        {
            loopLength_ = bufferSize_;
            mode_ = Mode::Playing;
            readPos_ = 0.0;
        }
        return input;

    case Mode::Playing:
        output = readInterpolated(readPos_);
        readPos_ += static_cast<double>(speed_);
        // Wrap in the effective (crossfade-aware) region
        {
            const double len = static_cast<double>(effectiveLength());
            readPos_ = std::fmod(readPos_, len);
            if (readPos_ < 0.0)
                readPos_ += len;
        }
        return input * (1.0f - mix_) + output * mix_;

    case Mode::Overdub:
    {
        const float existing = readInterpolated(readPos_);
        output = existing;
        const double len = static_cast<double>(effectiveLength());
        // Tape-style: write at read position. Skip write when frozen (speed=0)
        if (speed_ != 0.0f)
        {
            double wrappedPos = std::fmod(readPos_, len);
            if (wrappedPos < 0.0)
                wrappedPos += len;
            const int wp = static_cast<int>(wrappedPos);
            buffer_[static_cast<size_t>(wp)] = input + existing * feedback_ + 1e-25f;
        }
        readPos_ += static_cast<double>(speed_);
        readPos_ = std::fmod(readPos_, len);
        if (readPos_ < 0.0)
            readPos_ += len;
        return input * (1.0f - mix_) + output * mix_;
    }
    }

    return input;
}

} // namespace ideath
