#pragma once

namespace ideath {

/// Direct Form II Transposed biquad filter.
/// JUCE-free, real-time safe (no allocation after construction).
class Biquad
{
public:
    Biquad() = default;

    void reset();

    float process(float x);

    // Coefficient setters — all take Hz + Q + sampleRate.
    void setLowpass(float freqHz, float q, float sampleRate);
    void setHighpass(float freqHz, float q, float sampleRate);
    void setBandpass(float freqHz, float q, float sampleRate);

    // Direct coefficient access (for custom filter types).
    void setCoefficients(float b0, float b1, float b2, float a1, float a2);

    // Read-only access for testing.
    float getB0() const { return b0_; }
    float getB1() const { return b1_; }
    float getB2() const { return b2_; }
    float getA1() const { return a1_; }
    float getA2() const { return a2_; }

private:
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f;
    float a1_ = 0.0f, a2_ = 0.0f;
    float z1_ = 0.0f, z2_ = 0.0f;
};

} // namespace ideath
