#include <ideath/Wavefolder.h>
#include <algorithm>
#include <cmath>

namespace ideath {

void Wavefolder::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    reset();
}

void Wavefolder::reset()
{
    // Stateless — nothing to reset.
}

void Wavefolder::setDrive(float drive)
{
    // No upper bound — sin() output is inherently bounded to [-1,1]
    // regardless of drive. Higher drive = more folds.
    drive_ = std::max(1.0f, drive);
}

void Wavefolder::setMix(float mix)
{
    mix_ = std::clamp(mix, 0.0f, 1.0f);
}

float Wavefolder::process(float input)
{
    float wet = std::sin(input * drive_);
    return (1.0f - mix_) * input + mix_ * wet;
}

} // namespace ideath
