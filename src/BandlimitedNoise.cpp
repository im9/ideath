#include <ideath/BandlimitedNoise.h>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ideath {

BandlimitedNoise::BandlimitedNoise(uint32_t seed)
    : state_(seed)
{
    recalcCoef();
}

void BandlimitedNoise::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    lpState_ = 0.0f;
    recalcCoef();
}

void BandlimitedNoise::reset(uint32_t seed)
{
    state_ = seed;
    lpState_ = 0.0f;
}

void BandlimitedNoise::setBandwidth(float bandwidth)
{
    bandwidth_ = std::clamp(bandwidth, 0.0f, 1.0f);
    recalcCoef();
}

void BandlimitedNoise::recalcCoef()
{
    // Log-map Bandwidth → cutoff: 0.0 → 5 Hz, 1.0 → ~Nyquist (bypass).
    // We compute the standard one-pole LP coefficient from the cutoff:
    //   coef = 1 - exp(-2π * fc / fs)
    // and special-case the upper bound so 1.0 is exact bypass.
    if (bandwidth_ >= 1.0f)
    {
        coef_ = 1.0f;
        return;
    }

    constexpr float kMinHz = 5.0f;
    const float maxHz = sampleRate_ * 0.45f;
    if (maxHz <= kMinHz)
    {
        coef_ = 1.0f;
        return;
    }

    const float logMin = std::log(kMinHz);
    const float logMax = std::log(maxHz);
    const float fc = std::exp(logMin + (logMax - logMin) * bandwidth_);
    const float x = 2.0f * static_cast<float>(M_PI) * fc / sampleRate_;
    coef_ = 1.0f - std::exp(-x);
    // Numerical floor: keep coef in (0, 1].
    if (coef_ < 1e-7f) coef_ = 1e-7f;
    if (coef_ > 1.0f) coef_ = 1.0f;
}

float BandlimitedNoise::process()
{
    // xorshift32 — same stream as ideath::Noise so Bandwidth=1 is byte-equiv.
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;

    constexpr float kScale = 2.0f / 4294967295.0f;
    const float white = static_cast<float>(state_) * kScale - 1.0f;

    if (coef_ >= 1.0f)
    {
        lpState_ = white;
        return white;
    }

    // Standard one-pole IIR LP, with denormal DC offset on the feedback path.
    lpState_ += coef_ * (white - lpState_) + 1e-25f;
    return lpState_;
}

} // namespace ideath
