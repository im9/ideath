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
    clearState();
}

void PeakLimiter::clearState()
{
    std::fill(delayBuf_.begin(), delayBuf_.end(), 0.0f);
    std::fill(delayBufR_.begin(), delayBufR_.end(), 0.0f);
    // Silence: peak 0 (target 1), no reduction pending anywhere in the
    // window.
    std::fill(peakDelay_.begin(), peakDelay_.end(), 0.0f);
    std::fill(holdBuf_.begin(), holdBuf_.end(), 1.0f);
    holdSum_ = static_cast<double>(lookaheadSamples_);
    dequeHead_ = 0;
    dequeTail_ = 0;
    dequeCount_ = 0;
    sampleIndex_ = 0;
    delayWriteIdx_ = 0;
    envelopeGain_ = 1.0f;
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
    // Time constant: gain recovers to within 1/e in 'seconds'
    releaseCoeff_ = std::exp(-1.0f / (seconds * sampleRate_));
}

void PeakLimiter::setLookahead(float seconds)
{
    seconds = std::clamp(seconds, 0.0f, 0.01f); // max 10ms
    int newSize = std::max(1, static_cast<int>(seconds * sampleRate_));

    if (newSize != lookaheadSamples_)
    {
        const size_t n = static_cast<size_t>(newSize);
        lookaheadSamples_ = newSize;
        invLookahead_ = 1.0 / static_cast<double>(newSize);
        delayBuf_.assign(n, 0.0f);
        delayBufR_.assign(n, 0.0f);
        peakDelay_.assign(n, 0.0f);
        dequeVal_.assign(n, 1.0f);
        dequePos_.assign(n, 0);
        holdBuf_.assign(n, 1.0f);
        clearState();
    }
}

float PeakLimiter::computeGain(float peak)
{
    const int n = lookaheadSamples_;
    const int w = delayWriteIdx_;
    const uint32_t now = sampleIndex_;

    // 1. Target for this input sample.
    const float target = peak > thresholdLinear_ ? thresholdLinear_ / peak : 1.0f;

    // 2. Min-hold over the last n targets: monotonic deque in a ring of n
    //    (front = minimum).  First expire the entry leaving the window (at
    //    most one per sample; ages are unsigned differences, so the 32-bit
    //    index may wrap), so the ring never holds more than n entries.
    if (dequeCount_ > 0
        && now - dequePos_[static_cast<size_t>(dequeHead_)] >= static_cast<uint32_t>(n))
    {
        if (++dequeHead_ == n)
            dequeHead_ = 0;
        --dequeCount_;
    }
    while (dequeCount_ > 0)
    {
        const int back = dequeTail_ == 0 ? n - 1 : dequeTail_ - 1;
        if (dequeVal_[static_cast<size_t>(back)] < target)
            break;
        dequeTail_ = back;
        --dequeCount_;
    }
    dequeVal_[static_cast<size_t>(dequeTail_)] = target;
    dequePos_[static_cast<size_t>(dequeTail_)] = now;
    if (++dequeTail_ == n)
        dequeTail_ = 0;
    ++dequeCount_;
    const float hold = dequeVal_[static_cast<size_t>(dequeHead_)];
    sampleIndex_ = now + 1u;

    // 3. Running mean of the min-hold over n samples.
    holdSum_ += static_cast<double>(hold) - static_cast<double>(holdBuf_[static_cast<size_t>(w)]);
    holdBuf_[static_cast<size_t>(w)] = hold;
    if (w == n - 1)
    {
        // Rebuild once per window so additions and subtractions cannot
        // accumulate rounding.
        double s = 0.0;
        for (float v : holdBuf_)
            s += static_cast<double>(v);
        holdSum_ = s;
    }
    // n ones sum to exactly n, and n·(1/n) rounds to 1.0f, so an idle
    // limiter's gain is exactly 1.
    const float smooth = static_cast<float>(holdSum_ * invLookahead_);

    // 4. Attack follows the ramp; release is a one-pole upward.  Within
    //    1e−7 of the target the gain snaps onto it (no denormal tail, and
    //    a fully released limiter is exactly 1 → bit-exact passthrough).
    if (smooth < envelopeGain_ || smooth - envelopeGain_ < 1e-7f)
        envelopeGain_ = smooth;
    else
        envelopeGain_ = smooth + releaseCoeff_ * (envelopeGain_ - smooth);

    // 5. Exact ceiling: never above the target of the sample now leaving
    //    the delay (written n − 1 samples ago), taken with the current
    //    threshold — so a threshold lowered mid-stream holds from the next
    //    output on, even for peaks already inside the window.
    peakDelay_[static_cast<size_t>(w)] = peak;
    const int r = (w + 1 == n) ? 0 : w + 1;
    const float outPeak = peakDelay_[static_cast<size_t>(r)];
    const float outTarget = outPeak > thresholdLinear_ ? thresholdLinear_ / outPeak : 1.0f;
    currentGain_ = std::min(envelopeGain_, outTarget);
    return currentGain_;
}

float PeakLimiter::process(float input)
{
    const int w = delayWriteIdx_;
    delayBuf_[static_cast<size_t>(w)] = input;
    const float gain = computeGain(std::fabs(input));
    if (++delayWriteIdx_ >= lookaheadSamples_)
        delayWriteIdx_ = 0;
    return delayBuf_[static_cast<size_t>(delayWriteIdx_)] * gain;
}

std::pair<float, float> PeakLimiter::processStereo(float left, float right)
{
    const int w = delayWriteIdx_;
    delayBuf_[static_cast<size_t>(w)] = left;
    delayBufR_[static_cast<size_t>(w)] = right;
    const float gain = computeGain(std::max(std::fabs(left), std::fabs(right)));
    if (++delayWriteIdx_ >= lookaheadSamples_)
        delayWriteIdx_ = 0;
    const size_t r = static_cast<size_t>(delayWriteIdx_);
    return { delayBuf_[r] * gain, delayBufR_[r] * gain };
}

float PeakLimiter::getGainReductionDb() const
{
    if (currentGain_ <= 0.0f)
        return -100.0f;
    return 20.0f * std::log10(currentGain_);
}

} // namespace ideath
