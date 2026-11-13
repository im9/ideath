#include <ideath/UnisonOscillator.h>
#include <algorithm>
#include <cmath>

namespace ideath {

namespace {
constexpr float kTwoPi = 6.28318530718f;
}

void UnisonOscillator::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    for (int i = 0; i < kMaxVoices; ++i)
        voices_[i].prepare(sampleRate);
    reset();
}

void UnisonOscillator::reset()
{
    for (int i = 0; i < kMaxVoices; ++i)
    {
        voices_[i].reset();
        // Spread initial drift phases so voices are uncorrelated from
        // sample 0. Use an irrational fraction (golden ratio) to avoid
        // any periodic alignment with voice index.
        driftPhase_[i] = std::fmod(static_cast<float>(i) * 0.6180339887f, 1.0f);
    }

    // Per-voice slightly different drift rates. Without this, voices share
    // a single sine — phase-shifted but still correlated. Small random-ish
    // multipliers keep each voice's pitch wandering independently.
    static constexpr float kRateJitter[kMaxVoices] = {
        1.000f, 1.137f, 0.872f, 1.241f, 0.913f, 1.078f, 0.955f, 1.193f,
        0.831f, 1.064f, 1.158f, 0.897f, 1.022f, 1.211f, 0.943f, 1.105f
    };
    for (int i = 0; i < kMaxVoices; ++i)
        driftPhaseInc_[i] = (driftRateHz_ * kRateJitter[i]) / sampleRate_;
    driftInit_ = true;
}

void UnisonOscillator::setVoiceCount(int count)
{
    const int clamped = std::clamp(count, 1, kMaxVoices);
    // Unchanged → nothing to do (cheap per-sample calls; must not restart
    // the drift interval).
    if (clamped == voiceCount_)
        return;
    const int oldCount = voiceCount_;
    voiceCount_ = clamped;
    updateFrequencies();
    if (driftAmountCents_ > 0.0f && !driftInit_)
    {
        // Join the running interval: the new voices start at their LFO's
        // current value and reach the interval end with the others; the
        // voices already sounding are untouched.
        const int left = kDriftControlInterval - driftPos_;
        for (int i = oldCount; i < voiceCount_; ++i)
        {
            const float rNow = std::pow(2.0f, std::sin(driftPhase_[i] * kTwoPi)
                                                  * driftAmountCents_ / 1200.0f);
            driftPhase_[i] += driftPhaseInc_[i] * static_cast<float>(left);
            driftPhase_[i] -= std::floor(driftPhase_[i]);
            const float rEnd = std::pow(2.0f, std::sin(driftPhase_[i] * kTwoPi)
                                                  * driftAmountCents_ / 1200.0f);
            driftRatioStep_[i] = left > 0 ? (rEnd - rNow) / static_cast<float>(left) : 0.0f;
            driftRatio0_[i] = rNow - driftRatioStep_[i] * static_cast<float>(driftPos_);
        }
    }
}

void UnisonOscillator::setFrequency(float freqHz)
{
    frequency_ = std::clamp(freqHz, 0.0f, sampleRate_ * 0.5f);
    updateFrequencies();
}

void UnisonOscillator::setDetune(float cents)
{
    // No upper bound — extreme detune is musically valid (wide stereo, dissonance).
    const float clamped = std::max(0.0f, cents);
    // Unchanged → the per-voice frequencies are already current (drift, if
    // on, rewrites them every sample from baseVoiceFreq_; turning drift off
    // restores them).  Skips one pow per voice for callers that set the
    // detune every sample.
    if (clamped == detuneCents_)
        return;
    detuneCents_ = clamped;
    updateFrequencies();
}

void UnisonOscillator::setDriftAmount(float cents)
{
    const float clamped = std::max(0.0f, cents);
    if (clamped == driftAmountCents_)
        return;
    const bool wasOn = driftAmountCents_ > 0.0f;
    driftAmountCents_ = clamped;
    if (clamped == 0.0f)
    {
        // Back to the undrifted frequencies now — process() no longer
        // touches them once drift is off.
        for (int i = 0; i < voiceCount_; ++i)
            voices_[i].setFrequency(baseVoiceFreq_[i]);
    }
    else if (!wasOn)
    {
        driftInit_ = true;
    }
    // A change between two non-zero amounts lands at the next interval
    // boundary (≤ kDriftControlInterval samples).
}

void UnisonOscillator::setDriftRate(float hz)
{
    driftRateHz_ = std::max(0.0f, hz);
    static constexpr float kRateJitter[kMaxVoices] = {
        1.000f, 1.137f, 0.872f, 1.241f, 0.913f, 1.078f, 0.955f, 1.193f,
        0.831f, 1.064f, 1.158f, 0.897f, 1.022f, 1.211f, 0.943f, 1.105f
    };
    for (int i = 0; i < kMaxVoices; ++i)
        driftPhaseInc_[i] = (driftRateHz_ * kRateJitter[i]) / sampleRate_;
    // A new rate takes effect at the next interval boundary.
}

float UnisonOscillator::getVoiceDriftCents(int i) const
{
    if (i < 0 || i >= kMaxVoices || driftAmountCents_ <= 0.0f)
        return 0.0f;
    if (driftInit_ || i >= voiceCount_)
        return std::sin(driftPhase_[i] * kTwoPi) * driftAmountCents_;
    const double ratio = static_cast<double>(driftRatio0_[i])
                         + static_cast<double>(driftRatioStep_[i] * static_cast<float>(driftPos_));
    return static_cast<float>(1200.0 * std::log2(ratio));
}

void UnisonOscillator::updateFrequencies()
{
    if (voiceCount_ == 1)
    {
        baseVoiceFreq_[0] = frequency_;
        voices_[0].setFrequency(frequency_);
        return;
    }

    // Spread voices symmetrically: -detune/2 ... +detune/2
    for (int i = 0; i < voiceCount_; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(voiceCount_ - 1); // 0..1
        float offsetCents = detuneCents_ * (t - 0.5f); // -half..+half
        float ratio = std::pow(2.0f, offsetCents / 1200.0f);
        baseVoiceFreq_[i] = frequency_ * ratio;
        voices_[i].setFrequency(baseVoiceFreq_[i]);
    }
}

float UnisonOscillator::process(float waveform)
{
    // Drift path: only active when amount > 0, so the default behavior is
    // bit-identical to the pre-drift implementation (regression-safe).
    if (driftAmountCents_ > 0.0f)
    {
        // Control rate: at each interval boundary evaluate the LFO one
        // interval ahead and step the ratio linearly towards it.
        if (driftInit_ || driftPos_ >= kDriftControlInterval)
        {
            const float n = static_cast<float>(kDriftControlInterval);
            for (int i = 0; i < voiceCount_; ++i)
            {
                float r0;
                if (driftInit_)
                {
                    const float c = std::sin(driftPhase_[i] * kTwoPi) * driftAmountCents_;
                    r0 = std::pow(2.0f, c / 1200.0f);
                }
                else
                {
                    // End of the previous interval, exactly (no rounding
                    // carried from the lerp).
                    r0 = driftRatio0_[i] + driftRatioStep_[i] * n;
                }
                driftPhase_[i] += driftPhaseInc_[i] * n;
                driftPhase_[i] -= std::floor(driftPhase_[i]);
                const float c1 = std::sin(driftPhase_[i] * kTwoPi) * driftAmountCents_;
                const float r1 = std::pow(2.0f, c1 / 1200.0f);
                driftRatio0_[i] = r0;
                driftRatioStep_[i] = (r1 - r0) / n;
            }
            driftPos_ = 0;
            driftInit_ = false;
        }
        const float pos = static_cast<float>(driftPos_);
        for (int i = 0; i < voiceCount_; ++i)
            voices_[i].setFrequency(baseVoiceFreq_[i]
                                    * (driftRatio0_[i] + driftRatioStep_[i] * pos));
        ++driftPos_;
    }

    float sum = 0.0f;
    for (int i = 0; i < voiceCount_; ++i)
        sum += voices_[i].process(waveform);

    // Gain compensation: divide by sqrt(voiceCount) for perceptual balance
    return sum / std::sqrt(static_cast<float>(voiceCount_));
}

float UnisonOscillator::getFundamentalPhase() const
{
    return voices_[0].getPhase();
}

void UnisonOscillator::nudgeFundamentalPhase(float delta)
{
    for (int i = 0; i < voiceCount_; ++i)
        voices_[i].nudgePhase(delta);
}

} // namespace ideath
