#pragma once

namespace ideath {

/// Trapezoidal integrated state variable filter (TPT SVF).
/// Modulation-safe: stable under fast per-sample parameter changes.
/// Simultaneously produces LP/HP/BP/Notch from a single process call.
/// Based on Andrew Simper (Cytomic) equations.
class SVFilter
{
public:
    enum class Mode { Lowpass, Highpass, Bandpass, Notch };

    SVFilter();

    void prepare(float sampleRate);
    void reset();

    void setCutoff(float freqHz);
    void setResonance(float r);   // 0.0 – 1.0 (0 = no res, ~1 = self-oscillation)
    void setMode(Mode mode);

    float process(float x);

    // Multi-output: all filter types at once.
    struct Output { float low, high, band, notch; };
    Output processMulti(float x);

private:
    void updateCoefficients();

    float sampleRate_ = 44100.0f;
    float cutoffHz_   = 1000.0f;
    float resonance_  = 0.0f;
    Mode  mode_       = Mode::Lowpass;

    // Coefficients (Cytomic TPT).
    float g_  = 0.0f;
    float k_  = 2.0f;
    float a1_ = 0.0f;
    float a2_ = 0.0f;
    float a3_ = 0.0f;

    // State.
    float ic1eq_ = 0.0f;
    float ic2eq_ = 0.0f;
};

} // namespace ideath
