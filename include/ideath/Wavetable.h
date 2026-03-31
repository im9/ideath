#pragma once

#include <array>

namespace ideath {

/// Wavetable oscillator for chiptune synthesis.
/// Reads through a user-defined wave buffer using a phase accumulator.
/// Supports 4-bit (Game Boy style) and normalized table data.
class Wavetable
{
public:
    static constexpr int kMaxTableSize = 256;
    static constexpr int kDefaultTableSize = 32;

    enum class Interpolation { Nearest, Linear };

    Wavetable() = default;

    void prepare(float sampleRate);
    void reset();

    void setFrequency(float freqHz);
    void setInterpolation(Interpolation mode);

    /// Load a wavetable from 4-bit values (0-15), normalized to [-1, 1].
    /// Copies up to kMaxTableSize samples; size clamped to [1, kMaxTableSize].
    void setTable(const float* data, int size);

    /// Load already-normalized data (expected range [-1, 1]).
    void setTableNormalized(const float* data, int size);

    float process();

    float getPhase() const { return phase_; }
    int getTableSize() const { return tableSize_; }

    // --- Factory methods for built-in waveforms ---
    static Wavetable squareTable(int size = kDefaultTableSize);
    static Wavetable sawTable(int size = kDefaultTableSize);
    static Wavetable triangleTable(int size = kDefaultTableSize);
    static Wavetable sineTable(int size = kDefaultTableSize);

private:
    float sampleRate_ = 44100.0f;
    float phase_ = 0.0f;
    float phaseInc_ = 0.0f;
    int tableSize_ = kDefaultTableSize;
    Interpolation interpolation_ = Interpolation::Nearest;
    std::array<float, kMaxTableSize> table_{};
};

} // namespace ideath
