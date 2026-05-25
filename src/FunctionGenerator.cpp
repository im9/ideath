#include <ideath/FunctionGenerator.h>
#include <algorithm>
#include <cmath>

namespace ideath {

void FunctionGenerator::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    // Recompute increments against the new sample rate without forcing the
    // user to re-call setRise/setFall.
    {
        const float s = std::max(1.0f, riseTime_ * sampleRate_);
        phaseIncRise_ = 1.0f / s;
    }
    {
        const float s = std::max(1.0f, fallTime_ * sampleRate_);
        phaseIncFall_ = 1.0f / s;
    }
    reset();
}

void FunctionGenerator::reset()
{
    phase_ = 0.0f;
    currentValue_ = 0.0f;
    stage_ = Stage::Idle;
    eocPending_ = false;
    // cycle_ left at its user-set value: reset() is a state reset, not a
    // configuration reset.  Tests rely on cycle_ surviving reset.
}

void FunctionGenerator::setRise(float seconds)
{
    riseTime_ = std::clamp(seconds, kMinTime, kMaxRise);
    const float s = std::max(1.0f, riseTime_ * sampleRate_);
    phaseIncRise_ = 1.0f / s;
}

void FunctionGenerator::setFall(float seconds)
{
    fallTime_ = std::clamp(seconds, kMinTime, kMaxFall);
    const float s = std::max(1.0f, fallTime_ * sampleRate_);
    phaseIncFall_ = 1.0f / s;
}

void FunctionGenerator::setCurve(float curve)
{
    curve_ = std::clamp(curve, -1.0f, 1.0f);
    curveExponent_ = 1.0f + std::fabs(curve_) * kCurveK;
}

void FunctionGenerator::setCycle(bool on)
{
    cycle_ = on;
    if (on && stage_ == Stage::Idle)
    {
        // Auto-start from Idle so that turning cycle on behaves as an LFO
        // run-switch without needing an explicit trigger().
        phase_ = 0.0f;
        stage_ = Stage::Rise;
    }
}

void FunctionGenerator::trigger()
{
    if (stage_ == Stage::Fall)
    {
        // Mid-fall retrigger: pick the phase that reproduces the current
        // output, then switch to Rise so the next sample steps forward
        // from the same value (continuous, no reversal regardless of curve).
        phase_ = inverseShape(currentValue_);
        stage_ = Stage::Rise;
    }
    else if (stage_ == Stage::Idle)
    {
        phase_ = 0.0f;
        stage_ = Stage::Rise;
    }
    // stage == Rise: already rising, leave phase alone.  Restarting would
    // discard the current trajectory and produce a discontinuity.
}

float FunctionGenerator::shape(float t) const
{
    if (curve_ == 0.0f) return t;
    if (curve_ > 0.0f) return std::pow(t, curveExponent_);
    return 1.0f - std::pow(1.0f - t, curveExponent_);
}

float FunctionGenerator::inverseShape(float v) const
{
    v = std::clamp(v, 0.0f, 1.0f);
    if (curve_ == 0.0f) return v;
    const float invExp = 1.0f / curveExponent_;
    if (curve_ > 0.0f) return std::pow(v, invExp);
    return 1.0f - std::pow(1.0f - v, invExp);
}

bool FunctionGenerator::consumeEoc()
{
    const bool out = eocPending_;
    eocPending_ = false;
    return out;
}

float FunctionGenerator::process()
{
    switch (stage_)
    {
        case Stage::Idle:
            currentValue_ = 0.0f;
            break;

        case Stage::Rise:
        {
            phase_ += phaseIncRise_;
            if (phase_ >= 1.0f)
            {
                // Sample where phase crosses 1 is the peak.  Carry the
                // overshoot into Fall so total rise+fall length equals
                // (rise + fall) samples to within float rounding.
                const float overshoot = phase_ - 1.0f;
                currentValue_ = 1.0f;
                phase_ = overshoot;
                stage_ = Stage::Fall;
            }
            else
            {
                currentValue_ = shape(phase_);
            }
            break;
        }

        case Stage::Fall:
        {
            phase_ += phaseIncFall_;
            if (phase_ >= 1.0f)
            {
                const float overshoot = phase_ - 1.0f;
                currentValue_ = 0.0f;
                eocPending_ = true;
                if (cycle_)
                {
                    phase_ = overshoot;
                    stage_ = Stage::Rise;
                }
                else
                {
                    phase_ = 0.0f;
                    stage_ = Stage::Idle;
                }
            }
            else
            {
                currentValue_ = shape(1.0f - phase_);
            }
            break;
        }
    }
    return currentValue_;
}

} // namespace ideath
