#include <ideath/HarmonicOscillator.h>
#include <algorithm>
#include <cmath>

namespace ideath {

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;
} // namespace

HarmonicOscillator::HarmonicOscillator()
{
    reset();
    recomputePartialFreqs();
}

void HarmonicOscillator::prepare(float sampleRate)
{
    sampleRate_ = (sampleRate > 0.0f) ? sampleRate : 44100.0f;
    reset();
    recomputePartialFreqs();
}

void HarmonicOscillator::reset()
{
    // Re-seed phase RNG with the documented fixed constant.  xorshift32
    // produces well-decorrelated phases across the 32 partials so the
    // initial transient does not have a coherent peak.
    std::uint32_t state = kPhaseSeed;
    for (int i = 0; i < kMaxPartials; ++i)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        // Map uint32 to [0, 1) — divide by 2^32 (exact via float division).
        phase_[i] = static_cast<float>(state) * (1.0f / 4294967296.0f);
    }
}

void HarmonicOscillator::setFrequency(float hz)
{
    const float clamped = std::clamp(hz, kMinFreq, sampleRate_ * 0.45f);
    if (clamped == frequency_)
        return;
    frequency_ = clamped;
    recomputePartialFreqs();
}

void HarmonicOscillator::setPartialCount(int n)
{
    partialCount_ = std::clamp(n, 1, kMaxPartials);
}

void HarmonicOscillator::setPartialAmplitude(int idx, float amp)
{
    if (idx < 0 || idx >= kMaxPartials)
        return;
    amplitude_[idx] = std::clamp(amp, 0.0f, 1.0f);
}

void HarmonicOscillator::setBands(float low, float mid, float high, float shape)
{
    low   = std::clamp(low,   0.0f, 1.0f);
    mid   = std::clamp(mid,   0.0f, 1.0f);
    high  = std::clamp(high,  0.0f, 1.0f);
    shape = std::clamp(shape, 0.0f, 1.0f);

    // Band partitioning (0-indexed, half-open):
    //   LOW  = [0, 3)
    //   MID  = [3, 7)
    //   HIGH = [7, kMaxPartials)
    // Within-band taper:
    //   band_pos[i] = (i - start) / (width - 1) ∈ [0, 1]
    //   amplitude  = band_weight × (1 - shape × band_pos)
    auto fillBand = [&](int start, int endExcl, float weight)
    {
        const int width = endExcl - start;
        if (width <= 0)
            return;
        const float denom = (width > 1) ? static_cast<float>(width - 1) : 1.0f;
        for (int i = start; i < endExcl; ++i)
        {
            const float bandPos = (width > 1)
                ? static_cast<float>(i - start) / denom
                : 0.0f;
            const float within  = 1.0f - shape * bandPos;
            amplitude_[i] = weight * within;
        }
    };

    fillBand(0, 3,             low);
    fillBand(3, 7,             mid);
    fillBand(7, kMaxPartials,  high);
}

float HarmonicOscillator::process()
{
    float out = 0.0f;
    const int N = partialCount_;
    for (int i = 0; i < N; ++i)
    {
        if (!alive_[i])
            continue;
        // Phase wrap each sample (CLAUDE.md "Phase wrapping" convention).
        phase_[i] += phaseInc_[i];
        phase_[i] -= std::floor(phase_[i]);
        // Skip sin() when amplitude is exactly zero — non-trivial perf gain
        // when only one band is active (≈ ¾ of partials skipped).
        if (amplitude_[i] != 0.0f)
            out += amplitude_[i] * std::sin(kTwoPi * phase_[i]);
    }
    return out;
}

float HarmonicOscillator::getPartialAmplitude(int idx) const
{
    if (idx < 0 || idx >= kMaxPartials)
        return 0.0f;
    return amplitude_[idx];
}

bool HarmonicOscillator::isPartialAlive(int idx) const
{
    if (idx < 0 || idx >= kMaxPartials)
        return false;
    return alive_[idx];
}

void HarmonicOscillator::recomputePartialFreqs()
{
    const float nyquistLimit = sampleRate_ * 0.45f;
    for (int i = 0; i < kMaxPartials; ++i)
    {
        const float f = frequency_ * static_cast<float>(i + 1);
        if (f > nyquistLimit || f < 0.0f)
        {
            alive_[i]    = false;
            phaseInc_[i] = 0.0f;
        }
        else
        {
            alive_[i]    = true;
            phaseInc_[i] = f / sampleRate_;
        }
    }
}

} // namespace ideath
