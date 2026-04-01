#pragma once

namespace ideath {

/// Peak envelope compressor.
/// Threshold/ratio/makeup gain with smoothed attack/release.
class Compressor
{
public:
    Compressor();

    void prepare(float sampleRate);
    void reset();

    /// Threshold in dB (e.g., -20.0). Signal above this is compressed.
    void setThreshold(float dB);

    /// Compression ratio (1.0 = no compression, inf = limiting).
    void setRatio(float ratio);

    /// Attack time in seconds. How fast gain reduction kicks in.
    void setAttack(float seconds);

    /// Release time in seconds. How fast gain recovers.
    void setRelease(float seconds);

    /// Makeup gain in dB. Applied after compression.
    void setMakeup(float dB);

    /// Knee width in dB (0 = hard knee). Soft knee smooths the transition.
    void setKnee(float dB);

    /// Process one sample. Returns compressed output.
    float process(float input);

    /// Current gain reduction in dB (for metering, always <= 0).
    float getGainReductionDb() const;

private:
    float sampleRate_ = 44100.0f;
    float thresholdDb_ = -20.0f;
    float ratio_ = 4.0f;
    float attackCoeff_ = 0.0f;
    float releaseCoeff_ = 0.0f;
    float makeupLinear_ = 1.0f;
    float kneeDb_ = 0.0f;

    float envelope_ = 0.0f;   // peak envelope (linear)
    float gainDb_ = 0.0f;     // current gain reduction (dB, <= 0)
};

} // namespace ideath
