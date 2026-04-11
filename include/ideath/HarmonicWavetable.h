#pragma once

#include <array>
#include <vector>

namespace ideath {

/// Wavetable oscillator with 128 harmonic tables and continuous morphing.
/// Each table is generated via additive synthesis with increasing harmonic count.
/// Table 0 = fundamental only, table 127 = up to 128 harmonics (band-limited).
/// Designed for ambient/pad dual-oscillator architectures.
class HarmonicWavetable
{
public:
    static constexpr int kNumTables = 128;
    static constexpr int kTableSize = 256;

    HarmonicWavetable() = default;

    /// Generate all 128 tables via additive synthesis, band-limited to Nyquist.
    void prepare(float sampleRate);
    void reset();

    /// Set morph position: 0.0 = table 0 (fundamental), 1.0 = table 127 (full harmonics).
    void setMorphPosition(float pos);

    /// Set oscillator frequency in Hz.
    void setFrequency(float freqHz);

    /// Generate one sample. Linearly interpolates between adjacent tables
    /// and between adjacent samples within each table.
    float process();

    float getPhase() const { return phase_; }
    float getMorphPosition() const { return morphPos_; }

private:
    float sampleRate_ = 44100.0f;
    float phase_ = 0.0f;
    float phaseInc_ = 0.0f;
    float morphPos_ = 0.0f;

    // 128 tables × 256 samples, stored flat for cache locality
    std::vector<float> tables_;

    float readTable(int tableIndex, float phase) const;
};

} // namespace ideath
