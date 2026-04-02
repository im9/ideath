#include <ideath/Compressor.h>
#include <algorithm>
#include <cmath>

namespace ideath {

Compressor::Compressor()
{
    prepare(44100.0f);
}

void Compressor::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    setAttack(0.01f);
    setRelease(0.1f);
    setThreshold(-20.0f);
    setRatio(4.0f);
    setMakeup(0.0f);
    setKnee(0.0f);
    reset();
}

void Compressor::reset()
{
    envelope_ = 0.0f;
    gainDb_ = 0.0f;
}

void Compressor::setThreshold(float dB)
{
    thresholdDb_ = dB;
}

void Compressor::setRatio(float ratio)
{
    ratio_ = std::max(ratio, 1.0f);
}

void Compressor::setAttack(float seconds)
{
    seconds = std::max(seconds, 0.0001f);
    attackCoeff_ = std::exp(-1.0f / (seconds * sampleRate_));
}

void Compressor::setRelease(float seconds)
{
    seconds = std::max(seconds, 0.001f);
    releaseCoeff_ = std::exp(-1.0f / (seconds * sampleRate_));
}

void Compressor::setMakeup(float dB)
{
    makeupLinear_ = std::pow(10.0f, dB / 20.0f);
}

void Compressor::setKnee(float dB)
{
    kneeDb_ = std::max(dB, 0.0f);
}

float Compressor::process(float input)
{
    // Peak envelope follower
    float absIn = std::fabs(input);
    if (absIn > envelope_)
        envelope_ = absIn + attackCoeff_ * (envelope_ - absIn);
    else
        envelope_ = absIn + releaseCoeff_ * (envelope_ - absIn);

    // Flush denormals
    if (envelope_ < 1e-8f) envelope_ = 0.0f;

    // Convert envelope to dB
    float envDb = (envelope_ > 1e-6f) ? 20.0f * std::log10(envelope_) : -120.0f;

    // Compute gain reduction
    float gr = 0.0f; // gain reduction in dB (negative)

    if (kneeDb_ > 0.0f && envDb > (thresholdDb_ - kneeDb_ * 0.5f)
                       && envDb < (thresholdDb_ + kneeDb_ * 0.5f))
    {
        // Soft knee region: quadratic interpolation
        float x = envDb - thresholdDb_ + kneeDb_ * 0.5f;
        float slope = (1.0f - 1.0f / ratio_) / kneeDb_;
        gr = -slope * x * x * 0.5f;
    }
    else if (envDb > thresholdDb_)
    {
        // Above threshold (or above soft knee)
        float over = envDb - thresholdDb_;
        gr = -(over - over / ratio_);
    }

    // Smooth the gain reduction
    if (gr < gainDb_)
        gainDb_ = gr + attackCoeff_ * (gainDb_ - gr);
    else
        gainDb_ = gr + releaseCoeff_ * (gainDb_ - gr);

    // Apply gain
    float gainLinear = std::pow(10.0f, gainDb_ / 20.0f);

    return input * gainLinear * makeupLinear_;
}

float Compressor::getGainReductionDb() const
{
    return gainDb_;
}

} // namespace ideath
