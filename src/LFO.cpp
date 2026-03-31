#include <ideath/LFO.h>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ideath {

void LFO::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    reset();
}

void LFO::reset()
{
    phase_ = 0.0f;
    phaseInc_ = 0.0f;
    finished_ = false;
    holdValue_ = 0.0f;
    prevPhase_ = 0.0f;
}

void LFO::setRate(float rateHz)
{
    phaseInc_ = rateHz / sampleRate_;
}

void LFO::setWaveform(Waveform wf)
{
    waveform_ = wf;
}

void LFO::setPolarity(Polarity pol)
{
    polarity_ = pol;
}

void LFO::setOneShot(bool enabled)
{
    oneShot_ = enabled;
}

void LFO::trigger()
{
    phase_ = 0.0f;
    prevPhase_ = 0.0f;
    finished_ = false;
}

float LFO::process()
{
    if (finished_)
        return (polarity_ == Polarity::Unipolar) ? (holdValue_ + 1.0f) * 0.5f : holdValue_;

    prevPhase_ = phase_;
    phase_ += phaseInc_;

    // Check for one-shot completion
    if (oneShot_ && phase_ >= 1.0f)
    {
        phase_ = 1.0f;
        finished_ = true;
    }

    phase_ -= std::floor(phase_);

    // Generate bipolar output [-1, 1]
    float out = 0.0f;

    switch (waveform_)
    {
        case Waveform::Sine:
            out = std::sin(2.0f * static_cast<float>(M_PI) * phase_);
            break;

        case Waveform::Triangle:
            out = (phase_ < 0.5f)
                ? (4.0f * phase_ - 1.0f)
                : (3.0f - 4.0f * phase_);
            break;

        case Waveform::Square:
            out = (phase_ < 0.5f) ? 1.0f : -1.0f;
            break;

        case Waveform::Saw:
            out = 2.0f * phase_ - 1.0f;
            break;

        case Waveform::SampleAndHold:
            // New random value at phase wrap
            if (phase_ < prevPhase_)
            {
                // xorshift32
                noiseState_ ^= noiseState_ << 13;
                noiseState_ ^= noiseState_ >> 17;
                noiseState_ ^= noiseState_ << 5;
                constexpr float kScale = 2.0f / 4294967295.0f;
                holdValue_ = static_cast<float>(noiseState_) * kScale - 1.0f;
            }
            out = holdValue_;
            break;
    }

    if (finished_)
        holdValue_ = out;

    // Convert to unipolar if needed
    if (polarity_ == Polarity::Unipolar)
        out = (out + 1.0f) * 0.5f;

    return out;
}

} // namespace ideath
