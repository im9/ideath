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
    // The Off path is stateless; the ADAA memory and halfband histories
    // are cleared for the anti-aliased modes.
    prevInput_ = 0.0f;
    halfband_.reset();
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

void Wavefolder::setAntialiasing(Antialiasing mode)
{
    if (mode == mode_)
        return;
    mode_ = mode;
    reset();
}

float Wavefolder::latencySamples() const
{
    switch (mode_)
    {
        case Antialiasing::Adaa:   return 0.5f;
        case Antialiasing::Adaa2x: return detail::Halfband2x::kLatency + 0.25f; // + ½ sample at 2×
        case Antialiasing::Off:    break;
    }
    return 0.0f;
}

float Wavefolder::adaa(float x)
{
    // Mean of sin(d·t) over t ∈ [x₁, x]:
    //   (cos(d·x₁) − cos(d·x)) / (d·(x − x₁)) = sin(d·m) · sin(u) / u,
    //   m = (x + x₁)/2, u = d·(x − x₁)/2.
    // The dry path gets the same two-point mean m (identity's ADAA), so dry
    // and wet share the half-sample delay.
    const float m = 0.5f * (x + prevInput_);
    const float u = 0.5f * drive_ * (x - prevInput_);
    prevInput_ = x;
    if (mix_ == 0.0f)
        return m;
    // sin(u)/u: Taylor 1 − u²/6 below |u| = 1e−4 (next term u⁴/120 < 1e−18).
    const float sinc = std::fabs(u) < 1e-4f ? 1.0f - u * u * (1.0f / 6.0f) : std::sin(u) / u;
    const float wet = std::sin(drive_ * m) * sinc;
    return (1.0f - mix_) * m + mix_ * wet;
}

// Kept out of line so the plain Off path stays a small leaf function (the
// halfband code inlined into process() measurably slowed the Off path).
#if defined(__GNUC__)
__attribute__((noinline))
#endif
float Wavefolder::processAntialiased(float input)
{
    if (mode_ == Antialiasing::Adaa)
        return adaa(input);
    float even, odd;
    halfband_.upsample(input, even, odd);
    const float ye = adaa(even);
    const float yo = adaa(odd);
    return halfband_.downsample(ye, yo);
}

float Wavefolder::process(float input)
{
    if (mode_ != Antialiasing::Off)
        return processAntialiased(input);
    // Fully dry: (1 − 0)·x + 0·sin(d·x) = x whenever d·x is finite (0·sin
    // is ±0, and x + ±0 = x; for x = −0 the product is −0 as well, since
    // sin(−0·d) = −0).  Skip the sin.
    if (mix_ == 0.0f)
        return input;
    const float wet = std::sin(input * drive_);
    return (1.0f - mix_) * input + mix_ * wet;
}

} // namespace ideath
