#pragma once

#include <array>
#include <utility>
#include <vector>

namespace ideath {

/// Hall-style stereo reverb with pre-delay and LFO-modulated comb filters.
/// Produces a lush, evolving tail suited for large spaces.
/// Mono in, stereo out.
class HallReverb
{
public:
    HallReverb();

    void prepare(float sampleRate);
    void reset();

    /// Room size (0.0–1.0). Controls comb filter feedback (0.50–0.99).
    void setSize(float size);

    /// Damping (0.0–1.0). Low values = bright, open character.
    void setDamp(float damp);

    /// Pre-delay time in seconds (0.0–0.1).
    void setPreDelay(float seconds);

    /// LFO modulation depth (0.0–1.0). Controls chorus-like movement.
    void setModDepth(float depth);

    /// Dry/wet mix (0.0 = fully dry, 1.0 = fully wet).
    void setMix(float mix);

    /// Freeze mode. Infinite sustain when true.
    void setFreeze(bool freeze);

    /// Process one mono input sample. Returns {left, right}.
    std::pair<float, float> process(float input);

private:
    static constexpr int kNumCombs = 8;
    static constexpr int kNumAllpasses = 4;
    static constexpr float kInputGain = 0.015f;   // canonical Freeverb fixedgain
    static constexpr float kWetScale  = 3.0f;     // canonical Freeverb scalewet
    static constexpr int kStereoSpread = 23;
    static constexpr float kMaxPreDelay = 0.1f; // 100ms max

    float sampleRate_ = 44100.0f;
    float size_ = 0.7f;
    float damp_ = 0.1f;
    float modDepth_ = 0.5f;
    float mix_ = 0.5f;
    bool freeze_ = false;

    float feedback_ = 0.0f;
    float damp1_ = 0.0f;
    float damp2_ = 0.0f;

    void updateParams();

    // --- Pre-delay ---
    struct PreDelay
    {
        std::vector<float> buffer;
        int bufSize = 0;
        int writeIndex = 0;
        int delaySamples = 0;

        void prepare(int maxSamples);
        void clear();
        void setDelay(int samples);
        float process(float input);
    };

    // --- Modulated comb filter ---
    struct ModCombFilter
    {
        std::vector<float> buffer;
        int bufSize = 0;
        int writeIndex = 0;
        float filterStore = 0.0f;

        // Per-comb LFO state
        float lfoPhase = 0.0f;
        float lfoRate = 1.0f;   // Hz
        float lfoInc = 0.0f;    // phase increment per sample

        void setSize(int size);
        void clear();
        void setLfoRate(float rate, float sampleRate);
        float process(float input, float feedback, float damp1, float damp2,
                      float modDepth);
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

    PreDelay preDelay_;
    std::array<ModCombFilter, kNumCombs> combL_;
    std::array<ModCombFilter, kNumCombs> combR_;
    std::array<AllpassFilter, kNumAllpasses> allpassL_;
    std::array<AllpassFilter, kNumAllpasses> allpassR_;
};

} // namespace ideath
