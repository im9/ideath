#include <ideath/Oscillator.h>
#include <cmath>

namespace ideath {

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

    // Saw: 2*phase - 1  (range [-1, 1])
    const float saw = 2.0f * phase_ - 1.0f;

    // Square: +1 / -1
    const float square = phase_ < 0.5f ? 1.0f : -1.0f;

    // Morph: 0.0 = square, 1.0 = saw
    return square * (1.0f - waveform) + saw * waveform;
}

} // namespace ideath
