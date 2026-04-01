#pragma once

namespace ideath {

/// Sine-based wavefolder for West Coast timbre shaping.
/// Folds the input signal through sin(input * drive), producing
/// harmonically rich timbres as drive increases.
class Wavefolder
{
public:
    Wavefolder() = default;

    void prepare(float sampleRate);
    void reset();

    /// Set fold drive amount (>= 1.0). Higher values = more folds.
    void setDrive(float drive);

    /// Set dry/wet mix (0 = fully dry, 1 = fully wet).
    void setMix(float mix);

    /// Process one sample.
    float process(float input);

private:
    float sampleRate_ = 44100.0f;
    float drive_ = 1.0f;
    float mix_ = 1.0f;
};

} // namespace ideath
