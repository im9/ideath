#pragma once

#include <ideath/Antialiasing.h>

namespace ideath {

/// Soft-clipping saturation functions.
namespace Saturation {

/// tanh-based soft clip. drive: amount of gain before tanh (>= 1.0).
/// Memoryless and aliasing; see AntialiasedTanh for the anti-aliased form.
float tanhDrive(float input, float drive);

/// Simpler polynomial soft clip (cheaper than tanh).
float softClip(float input);

/// tanh(drive · x) with antiderivative anti-aliasing.
///
/// F(x) = log(cosh(d·x)) / d is the antiderivative of tanh(d·x); Adaa
/// outputs (F(x) − F(x₁)) / (x − x₁), the mean of tanh(d·t) over the line
/// from the previous input to the current one.  F is evaluated in double
/// as |u| + log1p(e^(−2|u|)) − ln 2 (no cosh overflow), and when
/// |x − x₁| < 1e−5 the quotient is replaced by tanh(d·(x + x₁)/2) (the
/// difference of two nearly equal F values is ill-conditioned there; the
/// midpoint value differs from the true mean by O(d²·Δ²) ≤ 1e−6 at d = 100).
/// F(x₁) is carried from the previous sample, so each step costs one
/// log-cosh.  Adaa2x runs the same at 2× between halfband filters (see
/// Antialiasing).  Off is bit-exact with tanhDrive().
///
/// Output: within ±1 for Off / Adaa (a mean of tanh values).  Adaa2x adds
/// the downsampling FIR's Gibbs overshoot: paper bound |y| ≤ Σ|h| = 1.59
/// (every 2× sample at ±1 with the sign of its tap).
class AntialiasedTanh
{
public:
    AntialiasedTanh() = default;

    void prepare(float sampleRate);
    void reset();

    /// Gain before tanh, clamped to >= 1 (as tanhDrive's documented range).
    void setDrive(float drive);

    /// Default Adaa.  Changing the mode resets the state.
    void setAntialiasing(Antialiasing mode);
    Antialiasing getAntialiasing() const { return mode_; }

    /// Delay in samples: 0 (Off), 0.5 (Adaa), 22.75 (Adaa2x).
    float latencySamples() const;

    float process(float input);

private:
    float adaa(float x);

    float sampleRate_ = 44100.0f;
    float drive_ = 1.0f;
    Antialiasing mode_ = Antialiasing::Adaa;
    float prevInput_ = 0.0f;
    double prevF_ = 0.0;        // log cosh(d·x₁) (not yet divided by d)
    float prevFDrive_ = 1.0f;   // drive prevF_ was computed with
    detail::Halfband2x halfband_;
};

} // namespace Saturation

} // namespace ideath
