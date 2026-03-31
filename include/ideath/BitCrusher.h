#pragma once

namespace ideath {

/// Bit depth reduction + sample rate reduction.
/// Emulates lo-fi digital converters (Game Boy, NES, etc.).
class BitCrusher
{
public:
    BitCrusher() = default;

    void prepare(float sampleRate);
    void reset();

    /// Set bit depth (1-32). Lower = more quantized.
    void setBitDepth(int bits);

    /// Set downsampling rate in Hz. The input is held until the next
    /// downsample step, creating the classic "staircase" effect.
    /// Pass 0 or >= sampleRate to disable downsampling.
    void setDownsampleRate(float rateHz);

    float process(float input);

private:
    float sampleRate_ = 44100.0f;
    int bitDepth_ = 32;
    float quantLevels_ = 4294967296.0f; // 2^32
    float downsampleInc_ = 1.0f;
    float downsamplePhase_ = 0.0f;
    float holdSample_ = 0.0f;
};

} // namespace ideath
