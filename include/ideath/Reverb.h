#pragma once

#include <array>
#include <utility>
#include <vector>

namespace ideath {

/// Freeverb-style stereo reverb.
/// 8 parallel comb filters + 4 series allpass filters.
/// Mono in, stereo out (mono-first exception for inherently stereo algorithm).
class Reverb
{
public:
    Reverb();

    /// Initialize with sample rate, allocate buffers, call reset().
    void prepare(float sampleRate);

    /// Clear all buffers and reset to initial state.
    void reset();

    /// Room size (0.0–1.0). Controls comb filter feedback.
    void setSize(float size);

    /// Damping (0.0–1.0). Controls high-frequency absorption.
    void setDamp(float damp);

    /// Dry/wet mix (0.0 = fully dry, 1.0 = fully wet).
    void setMix(float mix);

    /// Freeze mode. When true, feedback = 1.0, damp = 0 (infinite sustain).
    void setFreeze(bool freeze);

    /// Process one mono input sample. Returns {left, right}.
    std::pair<float, float> process(float input);

private:
    static constexpr int kNumCombs = 8;
    static constexpr int kNumAllpasses = 4;
    static constexpr float kInputGain = 0.015f;   // canonical Freeverb fixedgain
    static constexpr float kWetScale  = 3.0f;     // canonical Freeverb scalewet
    static constexpr int kStereoSpread = 23; // samples offset for R channel

    float sampleRate_ = 44100.0f;
    float size_ = 0.5f;
    float damp_ = 0.5f;
    float mix_ = 0.5f;
    bool freeze_ = false;

    // Derived parameters
    float feedback_ = 0.0f;
    float damp1_ = 0.0f;
    float damp2_ = 0.0f;

    void updateParams();

    // --- Comb filter ---
    struct CombFilter
    {
        std::vector<float> buffer;
        int bufSize = 0;
        int index = 0;
        float filterStore = 0.0f;

        void setSize(int size);
        void clear();
        float process(float input, float feedback, float damp1, float damp2);
    };

    // --- Allpass filter ---
    struct AllpassFilter
    {
        std::vector<float> buffer;
        int bufSize = 0;
        int index = 0;
        static constexpr float kFeedback = 0.5f;

        void setSize(int size);
        void clear();
        float process(float input);
    };

    std::array<CombFilter, kNumCombs> combL_;
    std::array<CombFilter, kNumCombs> combR_;
    std::array<AllpassFilter, kNumAllpasses> allpassL_;
    std::array<AllpassFilter, kNumAllpasses> allpassR_;
};

} // namespace ideath
