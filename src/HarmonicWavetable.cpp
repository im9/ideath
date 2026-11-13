#include <ideath/HarmonicWavetable.h>
#include <cmath>
#include <algorithm>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ideath {

namespace {

// Additive build of all tables: table t = Σ_{h=1}^{t+1} sin(2π·h·i/N) / h,
// peak-normalised to [−1, 1].  Each harmonic is computed in double and
// added to the float table per harmonic; keep this order of operations,
// the tests compare the tables bit for bit.
//
// No sample-rate band limit is needed: a band limit at the table's own
// Nyquist (h·sr/N > sr/2) only removes harmonics h > N/2 = 128, and
// t + 1 ≤ kNumTables = 128.  The tables are therefore the same at every
// rate and one set serves all instances.
std::vector<float> buildTables()
{
    constexpr int kNumTables = HarmonicWavetable::kNumTables;
    constexpr int kTableSize = HarmonicWavetable::kTableSize;
    static_assert(kNumTables <= kTableSize / 2,
                  "harmonic count must stay below the table's Nyquist");

    std::vector<float> tables(static_cast<size_t>(kNumTables) * kTableSize, 0.0f);
    const double twoPi = 2.0 * M_PI;

    for (int t = 0; t < kNumTables; ++t)
    {
        const int maxHarmonics = t + 1; // table 0 = 1 harmonic, table 127 = 128
        float* table = tables.data() + static_cast<size_t>(t) * kTableSize;

        for (int h = 1; h <= maxHarmonics; ++h)
        {
            const double amp = 1.0 / static_cast<double>(h);
            for (int i = 0; i < kTableSize; ++i)
            {
                table[i] += static_cast<float>(
                    amp * std::sin(twoPi * static_cast<double>(h)
                                   * static_cast<double>(i)
                                   / static_cast<double>(kTableSize)));
            }
        }

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
    return tables;
}

const float* sharedTables()
{
    // Function-local static: built once, thread-safe initialisation (C++11),
    // never freed or written afterwards, so process() reads it lock-free.
    static const std::vector<float> tables = buildTables();
    return tables.data();
}

} // namespace

void HarmonicWavetable::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    tables_ = sharedTables();
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
    if (tables_ == nullptr)
        return 0.0f; // not prepared yet
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
    const float* table = tables_ + static_cast<size_t>(tableIndex) * kTableSize;

    const float pos = phase * static_cast<float>(kTableSize);
    const int i0 = static_cast<int>(pos);
    const int idx0 = i0 % kTableSize;
    const int idx1 = (i0 + 1) % kTableSize;
    const float frac = pos - static_cast<float>(i0);

    return table[idx0] + frac * (table[idx1] - table[idx0]);
}

} // namespace ideath
