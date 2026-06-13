#include <ideath/LowPassGateVoice.h>
#include <algorithm>

namespace ideath {

void LowPassGateVoice::prepare(float sampleRate)
{
    carrier_.prepare(sampleRate);
    lpg_.prepare(sampleRate);
    reset();
}

void LowPassGateVoice::reset()
{
    carrier_.reset();
    lpg_.reset();
}

void LowPassGateVoice::setFrequency(float hz)
{
    carrier_.setFrequency(hz);
}

void LowPassGateVoice::setTone(float t)
{
    tone_ = std::clamp(t, 0.0f, 1.0f);
}

void LowPassGateVoice::setDamping(float d)
{
    lpg_.setDamping(d);
}

void LowPassGateVoice::setBrightness(float b)
{
    lpg_.setBrightness(b);
}

void LowPassGateVoice::ping(float velocity)
{
    lpg_.trigger(velocity);
}

float LowPassGateVoice::process()
{
    // Oscillator::process(morph) — 0 = square, 1 = saw.
    const float carrier = carrier_.process(tone_);
    return lpg_.process(carrier);
}

} // namespace ideath
