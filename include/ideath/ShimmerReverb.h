#pragma once

#include "Reverb.h"
#include <array>
#include <utility>
#include <vector>

namespace ideath {

/// Shimmer reverb — cross-coupled stereo allpass network with octave-shifted
/// pitch feedback. Produces ethereal, metallic, evolving tails.
/// Faust shimmer.dsp lineage.
/// Mono in, stereo out.
/// Output can reach ±6.0 for ±1.0 input (pitch-shift feedback regeneration
/// + kWetScale=3). Place a PeakLimiter downstream in the signal chain.
class ShimmerReverb
{
public:
    ShimmerReverb();

    void prepare(float sampleRate);
    void reset();

    /// Reverb size (0.0–1.0). Scales delay line lengths and diffusion.
    void setSize(float size);

    /// Damping (0.0–1.0). High-frequency absorption in feedback paths.
    void setDamp(float damp);

    /// Shimmer amount (0.0–1.0). Controls pitch-shifted feedback blend.
    void setShimmer(float amount);

    /// Dry/wet mix (0.0 = fully dry, 1.0 = fully wet).
    void setMix(float mix);

    /// Freeze mode. Infinite sustain when true.
    void setFreeze(bool freeze);

    /// Process one mono input sample. Returns {left, right}.
    std::pair<float, float> process(float input);

private:
    float sampleRate_ = 44100.0f;
    float size_ = 0.7f;
    float damp_ = 0.3f;
    float shimmer_ = 0.5f;
    float mix_ = 0.5f;
    bool freeze_ = false;
    bool prevFreeze_ = false;

    // Derived
    float feedback_ = 0.0f;
    float dampCoeff_ = 0.0f;

    // Freeze crossfade: shimmer → Freeverb over 200ms
    Reverb freezeReverb_;
    float freezeXfade_ = 0.0f;   // 0 = shimmer, 1 = freeverb
    float freezeXfadeInc_ = 0.0f;

    void updateParams();

    // --- Modulated allpass ---
    struct ModAllpass
    {
        std::vector<float> buffer;
        int bufSize = 0;
        int writeIndex = 0;
        float feedback = 0.5f;

        // LFO
        float lfoPhase = 0.0f;
        float lfoInc = 0.0f;
        float modDepth = 0.0f; // in samples

        void prepare(int size, float fb, float lfoRate, float phase, float sampleRate);
        void clear();
        float process(float input, float modScale);
    };

    // --- Fixed delay line ---
    struct FixedDelay
    {
        std::vector<float> buffer;
        int bufSize = 0;
        int index = 0;

        void prepare(int size);
        void clear();
        float process(float input);
    };

    // --- Pitch shifter (+12 semitones, dual-tap Hann crossfade) ---
    struct PitchShifter
    {
        static constexpr int kWindowSize = 2048;
        std::vector<float> buffer;
        int bufSize = 0;
        int writeIndex = 0;
        float phase1 = 0.0f;
        float phase2 = 0.5f; // 50% offset

        void prepare(int size);
        void clear();
        float process(float input);
    };

    // --- DC blocker (80 Hz HPF) ---
    struct DcBlocker
    {
        float prevIn = 0.0f;
        float prevOut = 0.0f;
        float coeff = 0.0f;

        void prepare(float sampleRate);
        void clear();
        float process(float input);
    };

    // --- One-pole LP (HF damping) ---
    struct DampFilter
    {
        float state = 0.0f;

        void clear() { state = 0.0f; }
        float process(float input, float coeff)
        {
            state = input + (state - input) * coeff;
            return state;
        }
    };

    // Forward allpass (input diffusion): 2 per channel
    // L: AP(601) → AP(613)
    // R: AP(2043) → AP(2087)
    std::array<ModAllpass, 2> fwdApL_;
    std::array<ModAllpass, 2> fwdApR_;

    // HF damping after forward allpass
    DampFilter dampL_;
    DampFilter dampR_;

    // DC blockers
    DcBlocker dcL_;
    DcBlocker dcR_;

    // Feedback allpass (cross-coupled): 2 per path
    // L feedback path (fed from R): AP(2337) → AP(2377)
    // R feedback path (fed from L): AP(1087) → AP(1113)
    std::array<ModAllpass, 2> fbApL_;
    std::array<ModAllpass, 2> fbApR_;

    // Fixed delays in feedback paths
    // L: delay(4325) before fbAP, delay(2969) after fbAP
    // R: delay(4763) before fbAP, delay(3111) after fbAP
    FixedDelay fbDelayPreL_;
    FixedDelay fbDelayPostL_;
    FixedDelay fbDelayPreR_;
    FixedDelay fbDelayPostR_;

    // Pitch shifters in feedback
    PitchShifter pitchL_;
    PitchShifter pitchR_;

    // Feedback state (cross-coupled)
    float fbL_ = 0.0f;
    float fbR_ = 0.0f;
};

} // namespace ideath
