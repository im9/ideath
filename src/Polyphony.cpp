#include "ideath/Polyphony.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace ideath {

void Polyphony::prepare(float sampleRate, int maxVoices)
{
    sampleRate_ = sampleRate;
    maxVoices_ = maxVoices;
    voices_.resize(static_cast<size_t>(maxVoices));
    voiceAge_.resize(static_cast<size_t>(maxVoices), 0);
    ageCounter_ = 0;

    for (auto& v : voices_)
        v.prepare(sampleRate);
}

void Polyphony::reset()
{
    for (auto& v : voices_)
        v.reset();
    std::fill(voiceAge_.begin(), voiceAge_.end(), 0);
    ageCounter_ = 0;
}

int Polyphony::findVoiceForNote() const
{
    // 1. Prefer idle voice
    for (int i = 0; i < maxVoices_; ++i)
    {
        if (!voices_[static_cast<size_t>(i)].isActive())
            return i;
    }

    // 2. Steal oldest active voice
    uint64_t oldest = std::numeric_limits<uint64_t>::max();
    int idx = 0;
    for (int i = 0; i < maxVoices_; ++i)
    {
        if (voiceAge_[static_cast<size_t>(i)] < oldest)
        {
            oldest = voiceAge_[static_cast<size_t>(i)];
            idx = i;
        }
    }
    return idx;
}

void Polyphony::noteOn(float freqHz, float velocity)
{
    int idx = findVoiceForNote();
    auto& v = voices_[static_cast<size_t>(idx)];
    v.reset(); // clean slate for stolen voices
    v.noteOn(freqHz, velocity);
    voiceAge_[static_cast<size_t>(idx)] = ++ageCounter_;
}

void Polyphony::noteOff(float freqHz)
{
    for (int i = 0; i < maxVoices_; ++i)
    {
        auto& v = voices_[static_cast<size_t>(i)];
        if (v.isActive() && std::abs(v.getFrequency() - freqHz) < 0.1f)
            v.noteOff();
    }
}

void Polyphony::allNotesOff()
{
    for (auto& v : voices_)
        v.noteOff();
}

float Polyphony::process()
{
    float mix = 0.0f;
    for (auto& v : voices_)
    {
        if (v.isActive())
            mix += v.process();
    }

    // Soft saturation via tanh.  The previous implementation hard-clipped
    // the raw voice sum to [-1, 1], which caused asymmetric harmonic
    // distortion whenever more than ~3 voices played simultaneously at
    // sustain.  Tests had to manually drop sustain to 0.3 to avoid the
    // resulting harshness — that's a hint that the mix stage was wrong,
    // not that the test was demanding too much.
    //
    // tanh(x) is ≈ x for |x| ≪ 1 (so a single quiet voice passes through
    // essentially untouched), saturates smoothly toward ±1 as the sum
    // grows, and never produces a hard discontinuity.  Listed in CLAUDE.md
    // backlog as "replace hard clip with soft saturation".
    return std::tanh(mix);
}

// --- Voice configuration (applied to all voices) ---

void Polyphony::setSource(Voice::Source src)
{
    for (auto& v : voices_) v.setSource(src);
}

void Polyphony::setOscWaveform(float wf)
{
    for (auto& v : voices_) v.setOscWaveform(wf);
}

void Polyphony::setWavetable(const float* data, int size)
{
    for (auto& v : voices_) v.setWavetable(data, size);
}

void Polyphony::setWavetableNormalized(const float* data, int size)
{
    for (auto& v : voices_) v.setWavetableNormalized(data, size);
}

void Polyphony::setAttack(float seconds)
{
    for (auto& v : voices_) v.setAttack(seconds);
}

void Polyphony::setDecay(float seconds)
{
    for (auto& v : voices_) v.setDecay(seconds);
}

void Polyphony::setSustain(float level)
{
    for (auto& v : voices_) v.setSustain(level);
}

void Polyphony::setRelease(float seconds)
{
    for (auto& v : voices_) v.setRelease(seconds);
}

void Polyphony::setFilter(Voice::FilterType type, float freqHz, float q)
{
    for (auto& v : voices_) v.setFilter(type, freqHz, q);
}

void Polyphony::setLfoRate(float rateHz)
{
    for (auto& v : voices_) v.setLfoRate(rateHz);
}

void Polyphony::setLfoPitchDepth(float semitones)
{
    for (auto& v : voices_) v.setLfoPitchDepth(semitones);
}

void Polyphony::setPortamento(float timeSec)
{
    for (auto& v : voices_) v.setPortamento(timeSec);
}

void Polyphony::setBitDepth(int bits)
{
    for (auto& v : voices_) v.setBitDepth(bits);
}

void Polyphony::setDownsampleRate(float rateHz)
{
    for (auto& v : voices_) v.setDownsampleRate(rateHz);
}

int Polyphony::getActiveVoiceCount() const
{
    int count = 0;
    for (const auto& v : voices_)
    {
        if (v.isActive())
            ++count;
    }
    return count;
}

bool Polyphony::hasActiveVoices() const
{
    for (const auto& v : voices_)
    {
        if (v.isActive())
            return true;
    }
    return false;
}

} // namespace ideath
