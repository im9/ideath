#pragma once

#include <ideath/Antialiasing.h>

namespace ideath {

/// Sine-based wavefolder for West Coast timbre shaping.
/// Folds the input signal through sin(input * drive), producing
/// harmonically rich timbres as drive increases.
///
/// Anti-aliasing (setAntialiasing, default Off = the plain 1× fold):
/// sin(d·x) has antiderivative −cos(d·x)/d, and the first-order ADAA mean
/// (F(x) − F(x₁)) / (x − x₁) is evaluated in the well-conditioned form
///   sin(d·m) · sin(u) / u,   m = (x + x₁)/2,  u = d·(x − x₁)/2
/// (cos a − cos b = −2·sin((a+b)/2)·sin((a−b)/2)), so there is no
/// cancellation as x − x₁ → 0 and no fallback branch beyond the sinc's
/// Taylor form at |u| < 1e−4.  The dry path is taken through the same
/// half-sample average (and, at 2×, the same halfband filters), so dry and
/// wet stay time-aligned at every mix — no comb filtering.
///
/// Output for |input| ≤ 1: ±1 (Off, Adaa).  Adaa2x adds the halfband FIRs'
/// Gibbs overshoot — paper bounds Σ|h| = 1.59 at mix 1 and
/// Σ|h|·2Σ|g| = 3.46 below mix 1 (the upsampled dry can overshoot too);
/// signals need sign-alternating content at the filter edges to come near
/// either.
class Wavefolder
{
public:
    Wavefolder() = default;

    void prepare(float sampleRate);
    void reset();

    /// Set fold drive amount (>= 1.0). Higher values = more folds.
    void setDrive(float drive);

    /// Set dry/wet mix (0 = fully dry, 1 = fully wet).
    void setMix(float mix);

    /// Anti-aliasing mode (see Antialiasing).  Changing it resets the
    /// anti-aliasing state.
    void setAntialiasing(Antialiasing mode);
    Antialiasing getAntialiasing() const { return mode_; }

    /// Delay of the output against the input in samples: 0 (Off),
    /// 0.5 (Adaa), 22.75 (Adaa2x).  Same for dry and wet.
    float latencySamples() const;

    /// Process one sample.  Off with mix 0 returns the input unchanged
    /// without evaluating sin (bit-exact with the full formula whenever
    /// input·drive is finite; past FLT_MAX the formula would give NaN).
    float process(float input);

private:
    float adaa(float x); // one ADAA step (dry/wet mixed) at the current rate
    float processAntialiased(float input);

    float sampleRate_ = 44100.0f;
    float drive_ = 1.0f;
    float mix_ = 1.0f;
    Antialiasing mode_ = Antialiasing::Off;
    float prevInput_ = 0.0f;  // x₁ for ADAA (at 2× in Adaa2x)
    detail::Halfband2x halfband_;
};

} // namespace ideath
