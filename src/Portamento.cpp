#include <ideath/Portamento.h>
#include <cmath>

namespace ideath {

void Portamento::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    reset();
}

void Portamento::reset()
{
    current_ = 0.0f;
    target_ = 0.0f;
    coef_ = 1.0f;
}

void Portamento::setTime(float timeSec)
{
    if (timeSec <= 0.0f)
    {
        coef_ = 1.0f; // instant
        return;
    }

    // Exponential approach: reach ~99.3% of target in timeSec
    // coef = 1 - exp(-1 / (time * sampleRate)) per sample
    float samples = timeSec * sampleRate_;
    coef_ = 1.0f - std::exp(-5.0f / samples);
}

void Portamento::setTarget(float target)
{
    target_ = target;
}

void Portamento::setValue(float value)
{
    current_ = value;
    target_ = value;
}

float Portamento::process()
{
    current_ += coef_ * (target_ - current_);
    return current_;
}

} // namespace ideath
