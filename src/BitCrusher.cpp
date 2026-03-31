#include <ideath/BitCrusher.h>
#include <cmath>
#include <algorithm>

namespace ideath {

void BitCrusher::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    reset();
}

void BitCrusher::reset()
{
    downsamplePhase_ = 0.0f;
    holdSample_ = 0.0f;
}

void BitCrusher::setBitDepth(int bits)
{
    bits = std::clamp(bits, 1, 32);
    bitDepth_ = bits;
    quantLevels_ = std::pow(2.0f, static_cast<float>(bits));
}

void BitCrusher::setDownsampleRate(float rateHz)
{
    if (rateHz <= 0.0f || rateHz >= sampleRate_)
        downsampleInc_ = 1.0f; // disabled
    else
        downsampleInc_ = rateHz / sampleRate_;
}

float BitCrusher::process(float input)
{
    // Sample rate reduction: hold the sample until phase wraps
    downsamplePhase_ += downsampleInc_;
    if (downsamplePhase_ >= 1.0f)
    {
        downsamplePhase_ -= std::floor(downsamplePhase_);

        // Bit depth reduction: quantize to 2^bits levels
        // Map [-1,1] → [0,1] → quantize → map back
        float normalized = (input + 1.0f) * 0.5f;  // [0, 1]
        float steps = quantLevels_ - 1.0f;
        normalized = std::floor(normalized * steps + 0.5f) / steps;
        holdSample_ = normalized * 2.0f - 1.0f;
    }

    return holdSample_;
}

} // namespace ideath
