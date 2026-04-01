#include <ideath/FeedbackBuffer.h>
#include <algorithm>

namespace ideath {

void FeedbackBuffer::prepare(float sampleRate, float maxLengthSec)
{
    sampleRate_ = sampleRate;
    bufferSize_ = static_cast<int>(maxLengthSec * sampleRate) + 1;
    buffer_.resize(static_cast<size_t>(bufferSize_), 0.0f);
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
        output = buffer_[static_cast<size_t>(readPos_)];
        ++readPos_;
        if (readPos_ >= loopLength_)
            readPos_ = 0;
        return input * (1.0f - mix_) + output * mix_;

    case Mode::Overdub:
    {
        float existing = buffer_[static_cast<size_t>(readPos_)];
        output = existing;
        // Mix new input with existing content scaled by feedback
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
