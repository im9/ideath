#include <ideath/UnisonOscillator.h>
#include <algorithm>
#include <cmath>

namespace ideath {

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
        voices_[i].reset();
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

void UnisonOscillator::updateFrequencies()
{
    if (voiceCount_ == 1)
    {
        voices_[0].setFrequency(frequency_);
        return;
    }

    // Spread voices symmetrically: -detune/2 ... +detune/2
    for (int i = 0; i < voiceCount_; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(voiceCount_ - 1); // 0..1
        float offsetCents = detuneCents_ * (t - 0.5f); // -half..+half
        float ratio = std::pow(2.0f, offsetCents / 1200.0f);
        voices_[i].setFrequency(frequency_ * ratio);
    }
}

float UnisonOscillator::process(float waveform)
{
    float sum = 0.0f;
    for (int i = 0; i < voiceCount_; ++i)
        sum += voices_[i].process(waveform);

    // Gain compensation: divide by sqrt(voiceCount) for perceptual balance
    return sum / std::sqrt(static_cast<float>(voiceCount_));
}

} // namespace ideath
