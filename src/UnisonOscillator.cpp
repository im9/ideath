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
}

void UnisonOscillator::setVoiceCount(int count)
{
    voiceCount_ = std::clamp(count, 1, kMaxVoices);
    updateFrequencies();
}

void UnisonOscillator::setFrequency(float freqHz)
{
    frequency_ = std::clamp(freqHz, 0.0f, sampleRate_ * 0.5f);
    updateFrequencies();
}

void UnisonOscillator::setDetune(float cents)
{
    // No upper bound — extreme detune is musically valid (wide stereo, dissonance).
    detuneCents_ = std::max(0.0f, cents);
    updateFrequencies();
}

void UnisonOscillator::setDriftAmount(float cents)
{
    driftAmountCents_ = std::max(0.0f, cents);
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
}

float UnisonOscillator::getVoiceDriftCents(int i) const
{
    if (i < 0 || i >= kMaxVoices)
        return 0.0f;
    return std::sin(driftPhase_[i] * kTwoPi) * driftAmountCents_;
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
        for (int i = 0; i < voiceCount_; ++i)
        {
            const float lfo = std::sin(driftPhase_[i] * kTwoPi);
            const float driftCents = lfo * driftAmountCents_;
            const float ratio = std::pow(2.0f, driftCents / 1200.0f);
            voices_[i].setFrequency(baseVoiceFreq_[i] * ratio);

            driftPhase_[i] += driftPhaseInc_[i];
            driftPhase_[i] -= std::floor(driftPhase_[i]);
        }
    }

    float sum = 0.0f;
    for (int i = 0; i < voiceCount_; ++i)
        sum += voices_[i].process(waveform);

    // Gain compensation: divide by sqrt(voiceCount) for perceptual balance
    return sum / std::sqrt(static_cast<float>(voiceCount_));
}

} // namespace ideath
