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
    readPos_ = 0;
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

void FeedbackBuffer::setCrossfade(float seconds)
{
    crossfadeSamples_ = std::max(0, static_cast<int>(seconds * sampleRate_));
}

void FeedbackBuffer::record()
{
    writePos_ = 0;
    readPos_ = 0;
    loopLength_ = 0;
    mode_ = Mode::Recording;
}

void FeedbackBuffer::stop()
{
    if (mode_ == Mode::Recording)
        loopLength_ = writePos_;
    mode_ = Mode::Stopped;
    readPos_ = 0;
}

void FeedbackBuffer::play()
{
    if (loopLength_ > 0)
    {
        readPos_ = 0;
        mode_ = Mode::Playing;
    }
}

void FeedbackBuffer::overdub()
{
    if (loopLength_ > 0)
    {
        readPos_ = 0;
        writePos_ = 0;
        mode_ = Mode::Overdub;
    }
}

float FeedbackBuffer::readSample(int pos) const
{
    float main = buffer_[static_cast<size_t>(pos)];

    if (crossfadeSamples_ <= 0 || loopLength_ <= crossfadeSamples_ * 2)
        return main;

    // Near end of loop: fade out main, fade in loop-start sample
    int distFromEnd = loopLength_ - 1 - pos;
    if (distFromEnd < crossfadeSamples_)
    {
        // fade: 1.0 at crossfade boundary, 0.0 at loop end
        float fade = static_cast<float>(distFromEnd) / static_cast<float>(crossfadeSamples_);
        // Corresponding sample from the start of the loop
        int startPos = crossfadeSamples_ - 1 - distFromEnd;
        float wrap = buffer_[static_cast<size_t>(startPos)];
        return main * fade + wrap * (1.0f - fade);
    }

    // Near start of loop: fade in main, fade out loop-end sample
    if (pos < crossfadeSamples_)
    {
        // fade: 0.0 at loop start, 1.0 at crossfade boundary
        float fade = static_cast<float>(pos) / static_cast<float>(crossfadeSamples_);
        // Corresponding sample from near the end of the loop
        int endPos = loopLength_ - crossfadeSamples_ + pos;
        float wrap = buffer_[static_cast<size_t>(endPos)];
        return main * fade + wrap * (1.0f - fade);
    }

    return main;
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
            readPos_ = 0;
        }
        return input;

    case Mode::Playing:
        output = readSample(readPos_);
        ++readPos_;
        if (readPos_ >= loopLength_)
            readPos_ = 0;
        return input * (1.0f - mix_) + output * mix_;

    case Mode::Overdub:
    {
        float existing = readSample(readPos_);
        output = existing;
        buffer_[static_cast<size_t>(writePos_)] = input + existing * feedback_;
        ++readPos_;
        ++writePos_;
        if (readPos_ >= loopLength_)
            readPos_ = 0;
        if (writePos_ >= loopLength_)
            writePos_ = 0;
        return input * (1.0f - mix_) + output * mix_;
    }
    }

    return input;
}

} // namespace ideath
