#pragma once

#include <array>

namespace ideath {

/// LFO-swept series-allpass phaser.
///
/// N first-order allpass stages in series, each stage's break frequency
/// tracked to a common LFO-swept centre.  Cumulative phase shift produces
/// notches in the sum (dry + wet); sweeping the centre moves the notches
/// through the spectrum.  This is a *distinct* colour from `Chorus`
/// (pitch wobble via delay modulation vs. spectral comb sweep via phase
/// modulation) — the two are complementary on ambient pads.
///
/// Feedback taps the last-stage output back into the first-stage input;
/// negative feedback accentuates the notches (deeper), positive
/// accentuates the peaks between notches (resonant sweep).
///
/// Each first-order allpass has unit gain everywhere; the cascade is
/// therefore also unit gain, so `mix + dry` sums stay in ±1.0 for ±1.0
/// input.  Denormal-guarded state.
///
/// Peak output ceiling: ±1.0 (documented in CLAUDE.md).  Feedback within
/// the clamped ±0.95 range keeps the loop pole inside the unit circle.
class Phaser
{
public:
    /// Max stages in the series cascade.  Two-stage minimum yields a single
    /// notch; each additional stage-pair adds one more notch.  Eight caps the
    /// depth of the effect at "classic MXR Phase 100"-like density.
    static constexpr int kMinStages = 2;
    static constexpr int kMaxStages = 8;

    Phaser() = default;

    void prepare(float sampleRate);
    void reset();

    /// Number of allpass stages.  Clamped to [kMinStages, kMaxStages].
    /// Default 4.  Even counts give symmetric notch pairs; odd counts add
    /// a half-turn phase shift that skews the sum.
    void setStages(int n);

    /// LFO rate in Hz.  Clamped to [0.01, 20].  Default 0.5.
    void setRate(float hz);

    /// Sweep depth, 0..1.  Interpreted as ± octaves around `centerHz`
    /// (depth = 1 → ±2 octaves = ×4 range).  Default 0.5.
    void setDepth(float amount);

    /// Sweep centre frequency in Hz.  Clamped to [50, 8000].
    /// Default 800 Hz — sits in the vowel region where pad movement is most
    /// audible.  The instantaneous per-stage break freq stays clamped to
    /// [20 Hz, 0.45·sr] regardless of centre × depth combos.
    void setCenterHz(float hz);

    /// Feedback amount, clamped to [-0.95, 0.95].  Default 0.
    void setFeedback(float amount);

    /// Dry/wet mix, clamped to [0, 1].  Default 0.5.
    void setMix(float wet);

    float process(float in);

    int getStages() const { return stages_; }

private:
    float sampleRate_ = 44100.0f;
    float rateHz_     = 0.5f;
    float phaseInc_   = 0.0f;
    float depth_      = 0.5f;
    float centerHz_   = 800.0f;
    float feedback_   = 0.0f;
    float mix_        = 0.5f;
    int   stages_     = 4;
    float phase_      = 0.0f;
    float lastOut_    = 0.0f;

    // Per-stage direct-form-I first-order allpass state.
    // y[n] = a x[n] + x[n-1] − a y[n-1]
    std::array<float, kMaxStages> prevX_{};
    std::array<float, kMaxStages> prevY_{};
};

} // namespace ideath
