#include <ideath/Oscillator.h>
#include <cmath>

namespace ideath {

// PolyBLEP correction for band-limiting discontinuities.
// t = current phase [0,1), dt = phase increment per sample.
static float polyblep(float t, float dt)
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt)
    {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

void Oscillator::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    reset();
}

void Oscillator::reset()
{
    phase_ = 0.0f;
    phaseInc_ = 0.0f;
}

void Oscillator::setFrequency(float freqHz)
{
    phaseInc_ = freqHz / sampleRate_;
}

float Oscillator::process(float waveform)
{
    phase_ += phaseInc_;
    phase_ -= std::floor(phase_);

    const float dt = phaseInc_;

    // Saw: 2*phase - 1, corrected at phase reset (0/1 boundary)
    float saw = 2.0f * phase_ - 1.0f;
    saw -= polyblep(phase_, dt);

    // Square: +1/-1, corrected at both transition points (0 and 0.5)
    float square = phase_ < 0.5f ? 1.0f : -1.0f;
    square += polyblep(phase_, dt);
    float phase05 = phase_ + 0.5f;
    phase05 -= std::floor(phase05);
    square -= polyblep(phase05, dt);

    // Morph: 0.0 = square, 1.0 = saw
    return square * (1.0f - waveform) + saw * waveform;
}

} // namespace ideath
