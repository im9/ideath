#pragma once

namespace ideath {

/// Wavetable oscillator with 128 harmonic tables and continuous morphing.
/// Each table is generated via additive synthesis with increasing harmonic count.
/// Table 0 = fundamental only, table 127 = up to 128 harmonics (band-limited).
/// Designed for ambient/pad dual-oscillator architectures.
///
/// The tables do not depend on the sample rate (table t holds harmonics
/// 1..t+1 ≤ 128 = kTableSize / 2, all below the table's own Nyquist), so one
/// set is built on first use and shared by every instance, read-only.
class HarmonicWavetable
{
public:
    static constexpr int kNumTables = 128;
    static constexpr int kTableSize = 256;

    HarmonicWavetable() = default;

    /// Bind the shared table set (built once per process, on the first
    /// prepare() of any instance — ≈ 10 ms; later calls cost nothing) and
    /// reset.  Thread-safe: the build is a function-local static.
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

    /// The shared tables, kNumTables × kTableSize floats, table-major
    /// (nullptr before prepare()).  For tests / inspection.
    const float* tableData() const { return tables_; }

private:
    float sampleRate_ = 44100.0f;
    float phase_ = 0.0f;
    float phaseInc_ = 0.0f;
    float morphPos_ = 0.0f;

    // Shared 128 tables × 256 samples, stored flat for cache locality.
    const float* tables_ = nullptr;

    float readTable(int tableIndex, float phase) const;
};

} // namespace ideath
