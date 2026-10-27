#pragma once

#include "Biquad.h"
#include "Reverb.h"
#include <array>
#include <utility>
#include <vector>

namespace ideath {

/// Shimmer reverb — stereo (independent L/R loops) allpass network with octave-shifted
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

    /// Reverb size (0.0–1.0). Sets the decay time: nominal T60 runs
    /// exponentially from 0.5 s (size 0) to the T60 of the 0.93 loop-gain
    /// ceiling (≈ 30 s, size 1). Also scales diffusion and modulation.
    void setSize(float size);

    /// Damping (0.0–1.0). High-frequency absorption in the feedback loop:
    /// attenuation at 4 kHz per loop pass rises linearly in dB from 0 to
    /// 12 dB, so the tail darkens evenly across the range.
    void setDamp(float damp);

    /// Shimmer amount (0.0–1.0). Equal-power blend of unshifted and
    /// octave-up signal in the feedback loop (0 = plain reverb,
    /// 1 = every pass is shifted up an octave).
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

    // Loop gain ceiling outside freeze. Below 1 so the direct (unshifted)
    // loop always decays; also the freeze-time loop gain.
    static constexpr float kMaxFeedback = 0.93f;
    // Nominal T60 at size 0.
    static constexpr float kMinT60 = 0.5f;
    // Per-pass damping at kDampRefHz when damp = 1.
    static constexpr float kMaxDampDb = 12.0f;
    static constexpr float kDampRefHz = 4000.0f;

    // Derived
    float feedback_ = 0.0f;
    float dampCoeff_ = 0.0f;
    float directGain_ = 0.0f;  // unshifted share of the feedback signal
    float shiftedGain_ = 0.0f; // octave-up share of the feedback signal
    float loopSeconds_ = 0.0f; // mean L/R direct-loop period

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

        // Peak LFO excursion at modScale = 1: 49 samples at 44.1 kHz,
        // scaled with sample rate like the delay lengths (≈ 1.1 ms).
        static constexpr float kModDepthAt44k = 49.0f;
        // modScale = 0.5 + 2.5·size ≤ 3.0.
        static constexpr float kMaxModScale = 3.0f;

        // Samples beyond the nominal delay reserved for the LFO excursion:
        // ceil(modDepth × kMaxModScale) plus 2 for the linear-interpolation
        // neighbour and rounding.
        int headroom = 0;

        // LFO
        float lfoPhase = 0.0f;
        float initialPhase = 0.0f; // restored by clear()
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
            // + 1e-25f denormal injection: this is a linear feedback state per
            // CLAUDE.md convention.  When `input` decays to zero (e.g. between
            // sustained passages), `state` would otherwise drift exponentially
            // toward denormal range at high `coeff`, causing the per-sample
            // FPU slowdown that denormal-flush guards against.
            state = input + (state - input) * coeff + 1e-25f;
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

    // Feedback allpass: 2 per path.  Each channel's loop feeds back into
    // itself (no L↔R coupling); loopSeconds_ is derived from these lengths.
    // L feedback path: AP(2337) → AP(2377)
    // R feedback path: AP(1087) → AP(1113)
    std::array<ModAllpass, 2> fbApL_;
    std::array<ModAllpass, 2> fbApR_;

    // Fixed delays in feedback paths
    // L: delay(4325) before fbAP, delay(2969) after fbAP
    // R: delay(4763) before fbAP, delay(3111) after fbAP
    FixedDelay fbDelayPreL_;
    FixedDelay fbDelayPostL_;
    FixedDelay fbDelayPreR_;
    FixedDelay fbDelayPostR_;

    // Pitch shifters in feedback, each behind an anti-alias lowpass
    PitchShifter pitchL_;
    PitchShifter pitchR_;
    Biquad pitchAaL_;
    Biquad pitchAaR_;

    // Feedback state (per channel)
    float fbL_ = 0.0f;
    float fbR_ = 0.0f;
};

} // namespace ideath
