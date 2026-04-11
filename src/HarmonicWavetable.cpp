#include <ideath/HarmonicWavetable.h>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ideath {

void HarmonicWavetable::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;

    tables_.resize(static_cast<size_t>(kNumTables) * kTableSize, 0.0f);

    const double twoPi = 2.0 * M_PI;
    const double nyquist = static_cast<double>(sampleRate) * 0.5;

    // Base frequency for band-limiting: assume middle-C (~261 Hz) as reference.
    // Higher played frequencies will naturally have fewer audible harmonics anyway.
    // For a 256-sample table at 44100 Hz, fundamental = sr / tableSize ≈ 172 Hz.
    // We use the table's intrinsic fundamental for band-limiting.
    const double tableBaseFreq = static_cast<double>(sampleRate)
                                 / static_cast<double>(kTableSize);

    for (int t = 0; t < kNumTables; ++t)
    {
        const int maxHarmonics = t + 1; // table 0 = 1 harmonic, table 127 = 128
        float* table = tables_.data()
                       + static_cast<size_t>(t) * kTableSize;

        // Clear
        for (int i = 0; i < kTableSize; ++i)
            table[i] = 0.0f;

        // Additive synthesis: sum of sin(2π·h·i/tableSize) / h
        for (int h = 1; h <= maxHarmonics; ++h)
        {
            if (static_cast<double>(h) * tableBaseFreq > nyquist)
                break;

            const double amp = 1.0 / static_cast<double>(h);
            for (int i = 0; i < kTableSize; ++i)
            {
                table[i] += static_cast<float>(
                    amp * std::sin(twoPi * static_cast<double>(h)
                                   * static_cast<double>(i)
                                   / static_cast<double>(kTableSize)));
            }
        }

        // Normalize table to [-1, 1]
        float peak = 0.0f;
        for (int i = 0; i < kTableSize; ++i)
            peak = std::max(peak, std::abs(table[i]));

        if (peak > 0.0f)
        {
            const float invPeak = 1.0f / peak;
            for (int i = 0; i < kTableSize; ++i)
                table[i] *= invPeak;
        }
    }

    reset();
}

void HarmonicWavetable::reset()
{
    phase_ = 0.0f;
}

void HarmonicWavetable::setMorphPosition(float pos)
{
    morphPos_ = std::clamp(pos, 0.0f, 1.0f);
}

void HarmonicWavetable::setFrequency(float freqHz)
{
    freqHz = std::clamp(freqHz, 0.0f, sampleRate_ * 0.45f);
    phaseInc_ = freqHz / sampleRate_;
}

float HarmonicWavetable::process()
{
    phase_ += phaseInc_;
    phase_ -= std::floor(phase_);

    // Map morph position to table index (fractional)
    const float tablePos = morphPos_ * static_cast<float>(kNumTables - 1);
    const int t0 = static_cast<int>(tablePos);
    const int t1 = std::min(t0 + 1, kNumTables - 1);
    const float tableFrac = tablePos - static_cast<float>(t0);

    // Read from both tables and crossfade
    const float s0 = readTable(t0, phase_);
    const float s1 = readTable(t1, phase_);
    return s0 + tableFrac * (s1 - s0);
}

float HarmonicWavetable::readTable(int tableIndex, float phase) const
{
    const float* table = tables_.data()
                         + static_cast<size_t>(tableIndex) * kTableSize;

    const float pos = phase * static_cast<float>(kTableSize);
    const int i0 = static_cast<int>(pos);
    const int idx0 = i0 % kTableSize;
    const int idx1 = (i0 + 1) % kTableSize;
    const float frac = pos - static_cast<float>(i0);

    return table[idx0] + frac * (table[idx1] - table[idx0]);
}

} // namespace ideath
