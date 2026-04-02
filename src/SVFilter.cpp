#include <ideath/SVFilter.h>
#include <cmath>
#include <algorithm>

namespace ideath {

void SVFilter::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    updateCoefficients();
    reset();
}

void SVFilter::reset()
{
    ic1eq_ = 0.0f;
    ic2eq_ = 0.0f;
}

void SVFilter::setCutoff(float freqHz)
{
    cutoffHz_ = std::clamp(freqHz, 5.0f, sampleRate_ * 0.45f);
    updateCoefficients();
}

void SVFilter::setResonance(float r)
{
    resonance_ = std::clamp(r, 0.0f, 0.99f);
    updateCoefficients();
}

void SVFilter::setMode(Mode mode)
{
    mode_ = mode;
}

SVFilter::Output SVFilter::processMulti(float x)
{
    const float v3 = x - ic2eq_;
    const float v1 = a1_ * ic1eq_ + a2_ * v3;
    const float v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3;
    ic1eq_ = 2.0f * v1 - ic1eq_;
    ic2eq_ = 2.0f * v2 - ic2eq_;

    const float low  = v2;
    const float band = v1;
    const float high = x - k_ * band - low;
    const float notch = low + high;

    return { low, high, band, notch };
}

float SVFilter::process(float x)
{
    auto out = processMulti(x);
    switch (mode_)
    {
        case Mode::Lowpass:  return out.low;
        case Mode::Highpass: return out.high;
        case Mode::Bandpass: return out.band;
        case Mode::Notch:    return out.notch;
    }
    return out.low;
}

void SVFilter::updateCoefficients()
{
    g_  = std::tan(static_cast<float>(M_PI) * cutoffHz_ / sampleRate_);
    k_  = 2.0f - 2.0f * resonance_;
    a1_ = 1.0f / (1.0f + g_ * (g_ + k_));
    a2_ = g_ * a1_;
    a3_ = g_ * a2_;
}

} // namespace ideath
