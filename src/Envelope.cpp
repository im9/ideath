#include <ideath/Envelope.h>
#include <cmath>
#include <algorithm>

namespace ideath {

// ---- DecayEnvelope ----

void DecayEnvelope::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    reset();
}

void DecayEnvelope::reset()
{
    level_ = 0.0f;
    active_ = false;
}

void DecayEnvelope::setDecay(float seconds)
{
    // Exponential decay coefficient: reaches ~-60 dB in `seconds`.
    // coef = exp(-6.9 / (seconds * sampleRate))  [6.9 ≈ ln(1000)]
    const float samples = std::max(1.0f, seconds * sampleRate_);
    decayCoef_ = std::exp(-6.9078f / samples);
}

void DecayEnvelope::trigger(float level)
{
    level_ = level;
    active_ = true;
}

float DecayEnvelope::process()
{
    if (!active_)
        return 0.0f;

    float out = level_;

    // Flush to zero before multiply to avoid denormals
    if (level_ < kSilenceThreshold)
    {
        level_ = 0.0f;
        active_ = false;
    }
    else
    {
        level_ *= decayCoef_;
    }

    return out;
}

// ---- AdsrEnvelope ----

float AdsrEnvelope::calcCoef(float timeSeconds, float sampleRate)
{
    const float samples = std::max(1.0f, timeSeconds * sampleRate);
    return std::exp(-6.9078f / samples);
}

void AdsrEnvelope::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    // ~1ms retrigger fade: exponential decay reaching -60 dB in 1ms.
    const float retriggerSamples = std::max(1.0f, 0.001f * sampleRate);
    retriggerCoef_ = std::exp(-6.9078f / retriggerSamples);
    reset();
}

void AdsrEnvelope::reset()
{
    level_ = 0.0f;
    stage_ = Stage::Idle;
}

void AdsrEnvelope::setAttack(float seconds)
{
    // Attack rate: linear rise from 0 to 1 in `seconds`.
    const float samples = std::max(1.0f, seconds * sampleRate_);
    attackRate_ = 1.0f / samples;
}

void AdsrEnvelope::setDecay(float seconds)
{
    decayCoef_ = calcCoef(seconds, sampleRate_);
}

void AdsrEnvelope::setSustain(float level)
{
    sustainLevel_ = level;
}

void AdsrEnvelope::setRelease(float seconds)
{
    releaseCoef_ = calcCoef(seconds, sampleRate_);
}

void AdsrEnvelope::noteOn()
{
    // If the envelope is still active with audible level, do a quick
    // retrigger fade (~1ms) before starting the new attack.  This prevents
    // clicks when the downstream signal chain (filter, saturation) carries
    // energy from the previous note.
    if (stage_ != Stage::Idle && level_ > 0.001f)
        stage_ = Stage::Retrigger;
    else
        stage_ = Stage::Attack;
}

void AdsrEnvelope::noteOff()
{
    if (stage_ != Stage::Idle)
        stage_ = Stage::Release;
}

float AdsrEnvelope::process()
{
    switch (stage_)
    {
        case Stage::Retrigger:
            level_ *= retriggerCoef_;
            if (level_ < 0.001f)
            {
                level_ = 0.0f;
                stage_ = Stage::Attack;
            }
            break;

        case Stage::Attack:
            level_ += attackRate_;
            if (level_ >= 1.0f)
            {
                level_ = 1.0f;
                stage_ = Stage::Decay;
            }
            break;

        case Stage::Decay:
        {
            // Exponential decay toward sustain level.
            float diff = level_ - sustainLevel_;
            if (diff < 1e-4f)
            {
                level_ = sustainLevel_;
                stage_ = Stage::Sustain;
            }
            else
            {
                level_ = sustainLevel_ + diff * decayCoef_;
            }
            break;
        }

        case Stage::Sustain:
            level_ = sustainLevel_;
            break;

        case Stage::Release:
            if (level_ < 1e-5f)
            {
                level_ = 0.0f;
                stage_ = Stage::Idle;
            }
            else
            {
                level_ *= releaseCoef_;
            }
            break;

        case Stage::Idle:
        default:
            level_ = 0.0f;
            break;
    }

    return level_;
}

} // namespace ideath
