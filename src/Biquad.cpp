#include <ideath/Biquad.h>
#include <cmath>

namespace ideath {

void Biquad::reset()
{
    z1_ = 0.0f;
    z2_ = 0.0f;
}

float Biquad::process(float x)
{
    const float y = b0_ * x + z1_;
    z1_ = b1_ * x - a1_ * y + z2_;
    z2_ = b2_ * x - a2_ * y;
    return y;
}

void Biquad::setCoefficients(float b0, float b1, float b2, float a1, float a2)
{
    b0_ = b0;
    b1_ = b1;
    b2_ = b2;
    a1_ = a1;
    a2_ = a2;
}

// RBJ Audio EQ Cookbook formulae.
// https://www.w3.org/2011/audio/audio-eq-cookbook.html

void Biquad::setLowpass(float freqHz, float q, float sampleRate)
{
    const float w0 = 2.0f * static_cast<float>(M_PI) * freqHz / sampleRate;
    const float cosw0 = std::cos(w0);
    const float sinw0 = std::sin(w0);
    const float alpha = sinw0 / (2.0f * q);

    const float a0 = 1.0f + alpha;
    b0_ = ((1.0f - cosw0) * 0.5f) / a0;
    b1_ = (1.0f - cosw0) / a0;
    b2_ = b0_;
    a1_ = (-2.0f * cosw0) / a0;
    a2_ = (1.0f - alpha) / a0;
}

void Biquad::setHighpass(float freqHz, float q, float sampleRate)
{
    const float w0 = 2.0f * static_cast<float>(M_PI) * freqHz / sampleRate;
    const float cosw0 = std::cos(w0);
    const float sinw0 = std::sin(w0);
    const float alpha = sinw0 / (2.0f * q);

    const float a0 = 1.0f + alpha;
    b0_ = ((1.0f + cosw0) * 0.5f) / a0;
    b1_ = -(1.0f + cosw0) / a0;
    b2_ = b0_;
    a1_ = (-2.0f * cosw0) / a0;
    a2_ = (1.0f - alpha) / a0;
}

void Biquad::setBandpass(float freqHz, float q, float sampleRate)
{
    const float w0 = 2.0f * static_cast<float>(M_PI) * freqHz / sampleRate;
    const float cosw0 = std::cos(w0);
    const float sinw0 = std::sin(w0);
    const float alpha = sinw0 / (2.0f * q);

    const float a0 = 1.0f + alpha;
    b0_ = alpha / a0;
    b1_ = 0.0f;
    b2_ = -alpha / a0;
    a1_ = (-2.0f * cosw0) / a0;
    a2_ = (1.0f - alpha) / a0;
}

} // namespace ideath
