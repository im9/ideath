#include <ideath/PeakLimiter.h>
#include <algorithm>
#include <cmath>

namespace ideath {

PeakLimiter::PeakLimiter()
{
    prepare(44100.0f);
}

void PeakLimiter::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;

    // Defaults
    setThreshold(0.0f);
    setRelease(0.1f);
    setLookahead(0.005f);

    reset();
}

void PeakLimiter::reset()
{
    std::fill(delayBuf_.begin(), delayBuf_.end(), 0.0f);
    delayWriteIdx_ = 0;
    envelope_ = 0.0f;
    currentGain_ = 1.0f;
}

void PeakLimiter::setThreshold(float dB)
{
    // Clamp to reasonable range
    dB = std::min(dB, 0.0f);
    thresholdLinear_ = std::pow(10.0f, dB / 20.0f);
}

void PeakLimiter::setRelease(float seconds)
{
    seconds = std::max(seconds, 0.001f);
    // Time constant: envelope decays to 1/e in 'seconds'
    releaseCoeff_ = std::exp(-1.0f / (seconds * sampleRate_));
}

void PeakLimiter::setLookahead(float seconds)
{
    seconds = std::clamp(seconds, 0.0f, 0.01f); // max 10ms
    int newSize = std::max(1, static_cast<int>(seconds * sampleRate_));

    if (newSize != static_cast<int>(delayBuf_.size()))
    {
        delayBuf_.resize(newSize, 0.0f);
        lookaheadSamples_ = newSize;
        delayWriteIdx_ = 0;
    }
}

float PeakLimiter::process(float input)
{
    // Write input to lookahead delay
    int readIdx = delayWriteIdx_;
    delayBuf_[delayWriteIdx_] = input;
    if (++delayWriteIdx_ >= lookaheadSamples_)
        delayWriteIdx_ = 0;

    // Read delayed signal (this is what we'll output)
    float delayed = delayBuf_[delayWriteIdx_];

    // Peak envelope follower on the input (not delayed)
    // This gives us lookahead: we see peaks before the audio arrives
    float absInput = std::fabs(input);

    if (absInput > envelope_)
        envelope_ = absInput; // instant attack
    else
        envelope_ = absInput + releaseCoeff_ * (envelope_ - absInput); // smooth release

    // Compute desired gain
    float targetGain = 1.0f;
    if (envelope_ > thresholdLinear_)
        targetGain = thresholdLinear_ / envelope_;

    // Smooth gain: instant attack (never overshoot), smooth release
    if (targetGain < currentGain_)
        currentGain_ = targetGain; // instant attack
    else
        currentGain_ = targetGain + releaseCoeff_ * (currentGain_ - targetGain);

    return delayed * currentGain_;
}

float PeakLimiter::getGainReductionDb() const
{
    if (currentGain_ <= 0.0f)
        return -100.0f;
    return 20.0f * std::log10(currentGain_);
}

} // namespace ideath
