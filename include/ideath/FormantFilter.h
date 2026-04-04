#pragma once

#include <ideath/SVFilter.h>

namespace ideath {

/// Formant filter — 3 parallel bandpass filters morphing between 5 vowels.
/// Input is mono, output is mono.  Morph parameter interpolates continuously
/// across A(0)–E(1)–I(2)–O(3)–U(4).
class FormantFilter
{
public:
    FormantFilter() = default;

    void prepare(float sampleRate);
    void reset();

    /// Set morph position: 0=A, 1=E, 2=I, 3=O, 4=U.
    /// Fractional values interpolate between adjacent vowels.
    void setMorph(float morph);

    /// Set resonance/Q for all three formant bands (0–1, default 0.5).
    /// Higher values give a more pronounced vocal character.
    void setResonance(float resonance);

    /// Set dry/wet mix (0 = fully dry, 1 = fully wet).
    void setMix(float mix);

    float process(float input);

private:
    void updateFormants();

    float sampleRate_ = 44100.0f;
    float morph_      = 0.0f;   // 0–4
    float resonance_  = 0.5f;   // 0–1
    float mix_        = 1.0f;   // 0–1

    // Per-band gain (interpolated from vowel table)
    float gain_[3] = { 1.0f, 1.0f, 1.0f };

    SVFilter band_[3];

    // Vowel formant table: [vowel][formant] = frequency in Hz
    static constexpr int kNumVowels = 5;
    static constexpr float kFreqs[kNumVowels][3] = {
        { 800.0f, 1150.0f, 2800.0f },  // A
        { 400.0f, 1600.0f, 2700.0f },  // E
        { 350.0f, 2300.0f, 3000.0f },  // I
        { 450.0f,  800.0f, 2830.0f },  // O
        { 325.0f,  700.0f, 2530.0f },  // U
    };

    // Relative gain per formant (higher formants are naturally quieter)
    static constexpr float kGains[kNumVowels][3] = {
        { 1.0f, 0.5f, 0.25f },  // A — open, strong F1
        { 1.0f, 0.7f, 0.3f  },  // E
        { 1.0f, 0.5f, 0.2f  },  // I — closed, weak upper
        { 1.0f, 0.5f, 0.2f  },  // O
        { 1.0f, 0.4f, 0.2f  },  // U — closed, weakest upper
    };
};

} // namespace ideath
