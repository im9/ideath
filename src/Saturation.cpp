#include <ideath/Saturation.h>
#include <algorithm>
#include <cmath>

namespace ideath {
namespace Saturation {

float tanhDrive(float input, float drive)
{
    return std::tanh(input * drive);
}

float softClip(float input)
{
    // Cubic soft clip: y = x - x^3/3  for |x| <= 1, else ±2/3.
    if (input > 1.0f)
        return 2.0f / 3.0f;
    if (input < -1.0f)
        return -2.0f / 3.0f;
    return input - (input * input * input) / 3.0f;
}

namespace {

// log(cosh(u)) without overflow: cosh u = e^|u|·(1 + e^(−2|u|))/2.
double logCosh(double u)
{
    const double a = std::fabs(u);
    return a + std::log1p(std::exp(-2.0 * a)) - 0.69314718055994530942;
}

// Below this |x − x₁| the F difference is ill-conditioned; use the
// midpoint (see header).
constexpr double kMinStep = 1e-5;

} // namespace

void AntialiasedTanh::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    reset();
}

void AntialiasedTanh::reset()
{
    prevInput_ = 0.0f;
    prevF_ = 0.0;          // log cosh(0) = 0
    prevFDrive_ = drive_;
    halfband_.reset();
}

void AntialiasedTanh::setDrive(float drive)
{
    drive_ = std::max(1.0f, drive);
}

void AntialiasedTanh::setAntialiasing(Antialiasing mode)
{
    if (mode == mode_)
        return;
    mode_ = mode;
    reset();
}

float AntialiasedTanh::latencySamples() const
{
    switch (mode_)
    {
        case Antialiasing::Adaa:   return 0.5f;
        case Antialiasing::Adaa2x: return detail::Halfband2x::kLatency + 0.25f;
        case Antialiasing::Off:    break;
    }
    return 0.0f;
}

float AntialiasedTanh::adaa(float x)
{
    const double d = static_cast<double>(drive_);
    // F(x₁) was computed with the drive of the previous step; recompute it
    // when the drive moved, so both ends use the same antiderivative.
    if (prevFDrive_ != drive_)
    {
        prevF_ = logCosh(d * static_cast<double>(prevInput_));
        prevFDrive_ = drive_;
    }
    const double f = logCosh(d * static_cast<double>(x));
    const double step = static_cast<double>(x) - static_cast<double>(prevInput_);
    float y;
    if (std::fabs(step) < kMinStep)
        y = std::tanh((0.5f * (x + prevInput_)) * drive_);
    else
        y = static_cast<float>((f - prevF_) / (d * step));
    prevInput_ = x;
    prevF_ = f;
    return y;
}

float AntialiasedTanh::process(float input)
{
    switch (mode_)
    {
        case Antialiasing::Adaa:
            return adaa(input);
        case Antialiasing::Adaa2x:
        {
            float even, odd;
            halfband_.upsample(input, even, odd);
            const float ye = adaa(even);
            const float yo = adaa(odd);
            return halfband_.downsample(ye, yo);
        }
        case Antialiasing::Off:
            break;
    }
    return tanhDrive(input, drive_);
}

} // namespace Saturation
} // namespace ideath
