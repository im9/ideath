#include <ideath/TapeDelay.h>
#include <ideath/Saturation.h>
#include <algorithm>
#include <cmath>

namespace ideath {

static constexpr float kAntiDenormal = 1e-25f;
static constexpr float kTwoPi = 6.283185307f;

void TapeDelay::prepare(float sampleRate, float maxDelaySec)
{
    sampleRate_ = sampleRate;
    maxDelaySamples_ = std::max(1.0f, maxDelaySec * sampleRate_);
    bufferSize_ = static_cast<int>(maxDelaySamples_) + 2;
    buffer_.resize(static_cast<size_t>(bufferSize_), 0.0f);

    highpass_.reset();
    lowpass_.reset();
    updateFeedbackFilters();

    setWowRate(0.3f);
    setFlutterRate(4.0f);
    reset();
}

void TapeDelay::reset()
{
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    writePos_ = 0;
    wowPhase_ = 0.0f;
    flutterPhase_ = 0.0f;
    highpass_.reset();
    lowpass_.reset();
}

void TapeDelay::setDelay(float delaySec)
{
    delaySamples_ = std::clamp(delaySec * sampleRate_, 1.0f, maxDelaySamples_);
}

void TapeDelay::setFeedback(float feedback)
{
    feedback_ = std::clamp(feedback, -0.999f, 0.999f);
}

void TapeDelay::setMix(float mix)
{
    mix_ = std::clamp(mix, 0.0f, 1.0f);
}

void TapeDelay::setWowDepth(float seconds)
{
    const float maxDepth = std::min(0.05f * sampleRate_, maxDelaySamples_ * 0.25f);
    wowDepthSamples_ = std::clamp(seconds * sampleRate_, 0.0f, maxDepth);
}

void TapeDelay::setWowRate(float rateHz)
{
    rateHz = std::clamp(rateHz, 0.01f, 10.0f);
    wowInc_ = rateHz / sampleRate_;
}

void TapeDelay::setFlutterDepth(float seconds)
{
    const float maxDepth = std::min(0.01f * sampleRate_, maxDelaySamples_ * 0.1f);
    flutterDepthSamples_ = std::clamp(seconds * sampleRate_, 0.0f, maxDepth);
}

void TapeDelay::setFlutterRate(float rateHz)
{
    rateHz = std::clamp(rateHz, 0.1f, 20.0f);
    flutterInc_ = rateHz / sampleRate_;
}

void TapeDelay::setLowpass(float freqHz)
{
    lowpassHz_ = freqHz;
    updateFeedbackFilters();
}

void TapeDelay::setHighpass(float freqHz)
{
    highpassHz_ = freqHz;
    updateFeedbackFilters();
}

void TapeDelay::setDrive(float drive)
{
    drive_ = std::max(1.0f, drive);
}

void TapeDelay::updateFeedbackFilters()
{
    const float hp = std::clamp(highpassHz_, 5.0f, sampleRate_ * 0.45f);
    const float lp = std::clamp(lowpassHz_, hp + 5.0f, sampleRate_ * 0.45f);
    highpass_.setHighpass(hp, 0.707f, sampleRate_);
    lowpass_.setLowpass(lp, 0.707f, sampleRate_);
}

float TapeDelay::readDelay(float delaySamples) const
{
    float readPos = static_cast<float>(writePos_) - delaySamples;
    while (readPos < 0.0f)
        readPos += static_cast<float>(bufferSize_);

    int i0 = static_cast<int>(readPos);
    int i1 = i0 + 1;
    if (i0 >= bufferSize_) i0 -= bufferSize_;
    if (i1 >= bufferSize_) i1 -= bufferSize_;

    float frac = readPos - std::floor(readPos);
    return buffer_[static_cast<size_t>(i0)]
         + frac * (buffer_[static_cast<size_t>(i1)] - buffer_[static_cast<size_t>(i0)]);
}

float TapeDelay::process(float input)
{
    float wow = std::sin(kTwoPi * wowPhase_);
    float flutter = std::sin(kTwoPi * flutterPhase_);

    wowPhase_ += wowInc_;
    flutterPhase_ += flutterInc_;
    wowPhase_ -= std::floor(wowPhase_);
    flutterPhase_ -= std::floor(flutterPhase_);

    float modulatedDelay = delaySamples_ + wow * wowDepthSamples_ + flutter * flutterDepthSamples_;
    modulatedDelay = std::clamp(modulatedDelay, 1.0f, maxDelaySamples_);

    float wet = readDelay(modulatedDelay);

    float feedbackSample = highpass_.process(wet);
    feedbackSample = lowpass_.process(feedbackSample);
    feedbackSample = Saturation::tanhDrive(feedbackSample, drive_);

    buffer_[static_cast<size_t>(writePos_)] = input + feedbackSample * feedback_ + kAntiDenormal;

    ++writePos_;
    if (writePos_ >= bufferSize_)
        writePos_ = 0;

    return input * (1.0f - mix_) + wet * mix_;
}

} // namespace ideath
