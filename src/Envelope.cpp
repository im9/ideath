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
    sustainLevel_ = std::clamp(level, 0.0f, 1.0f);
}

void AdsrEnvelope::setRelease(float seconds)
{
    releaseCoef_ = calcCoef(seconds, sampleRate_);
}

void AdsrEnvelope::setCurve(float curve)
{
    curve_ = std::clamp(curve, -1.0f, 1.0f);
    // Map -1 → 0.5, 0 → 1.0, +1 → 2.0 (exponential family).  Curve == 0
    // yields exponent 1.0 so pow(x, 1) is a no-op and the legacy linear
    // attack / exponential release shape is preserved bit-for-bit.
    curveExponent_ = std::pow(2.0f, curve_);
}

void AdsrEnvelope::noteOn()
{
    // If the envelope is still active with audible level, do a quick
    // retrigger fade (~1ms) before starting the new attack.  This prevents
    // clicks when the downstream signal chain (filter, saturation) carries
    // energy from the previous note.
    if (stage_ != Stage::Idle && level_ > 0.001f)
    {
        // Curve-aware retrigger: reuse the release-curve normalisation so
        // the fade stays continuous with the previous segment regardless
        // of curve_.  When we're already in Release we keep the existing
        // releaseStartLevel_ from noteOff (the release branch's reference
        // point) — overwriting it would break continuity with the curved
        // release.  From any other stage we capture the current level_
        // as the new reference.
        if (stage_ != Stage::Release)
            releaseStartLevel_ = level_;
        stage_ = Stage::Retrigger;
    }
    else
    {
        stage_ = Stage::Attack;
    }
}

void AdsrEnvelope::noteOff()
{
    if (stage_ != Stage::Idle)
    {
        // Capture the level at the moment release begins so the curve
        // shaper can normalise around it (avoids a discontinuity at the
        // sustain→release boundary when curve_ != 0).
        releaseStartLevel_ = level_;
        stage_ = Stage::Release;
    }
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

    // --- Curve shaping ---
    //
    // The internal `level_` state is left untouched (so the existing linear
    // attack ramp + exponential release timing are preserved exactly when
    // curve_ == 0).  Curve only reshapes the *output* of the attack and
    // release segments via a power law, with the release branch normalised
    // against the level captured at noteOff so the segment boundary stays
    // continuous regardless of curve.
    //
    // Implementation choice (option A from the spec): a simple pow() shaper
    // is the cheapest path that integrates with the existing calcCoef-based
    // engine without disturbing any of its timing math.
    if (curve_ == 0.0f)
        return level_;

    if (stage_ == Stage::Attack)
    {
        // level_ rises 0 → 1 linearly, so pow(level_, exponent) gives the
        // standard upward-bent attack family.
        return std::pow(level_, curveExponent_);
    }
    if ((stage_ == Stage::Release || stage_ == Stage::Retrigger)
        && releaseStartLevel_ > 0.0f)
    {
        // Normalise: at the start of release/retrigger level_ ==
        // releaseStartLevel_ so output == releaseStartLevel_ (continuous
        // with the previous segment).  As level_ → 0 the output also → 0.
        // exponent > 1 → faster end, exponent < 1 → longer tail.
        //
        // Retrigger uses the same formula because it is also an
        // exponential decay toward zero (just with a much faster
        // coefficient).  Without this branch, a noteOn during a curved
        // release would jump from `releaseStartLevel_ * pow(...)` back
        // up to the bare `level_`, producing exactly the click the
        // retrigger fade is supposed to suppress.
        const float ratio = level_ / releaseStartLevel_;
        return releaseStartLevel_ * std::pow(ratio, curveExponent_);
    }
    return level_;
}

// ---- AREnvelope ----

void AREnvelope::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    reset();
}

void AREnvelope::reset()
{
    level_ = 0.0f;
    stage_ = Stage::Idle;
}

void AREnvelope::setAttack(float seconds)
{
    // Linear rise from 0 to 1 in `seconds`, matching AdsrEnvelope's attack shape.
    const float samples = std::max(1.0f, seconds * sampleRate_);
    attackRate_ = 1.0f / samples;
}

void AREnvelope::setRelease(float seconds)
{
    // Exponential decay reaching ~-60 dB in `seconds`.
    const float samples = std::max(1.0f, seconds * sampleRate_);
    releaseCoef_ = std::exp(-6.9078f / samples);
}

void AREnvelope::noteOn()
{
    stage_ = Stage::Attack;
}

void AREnvelope::noteOff()
{
    if (stage_ != Stage::Idle)
        stage_ = Stage::Release;
}

float AREnvelope::process()
{
    switch (stage_)
    {
        case Stage::Attack:
            level_ += attackRate_;
            if (level_ >= 1.0f)
            {
                level_ = 1.0f;
                stage_ = Stage::Sustain;
            }
            break;

        case Stage::Sustain:
            level_ = 1.0f;
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
