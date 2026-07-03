#pragma once

#include <vector>

namespace ideath {

/// Schroeder allpass filter — flat-magnitude diffusor.
///
/// Transfer function: H(z) = (-g + z^-N) / (1 - g z^-N)
/// with |H(e^jω)| = 1 at every frequency (paper-perfect allpass).
///
/// Standalone building block for diffusion / shimmer networks
/// (Freeverb-style tail construction, Gardner reverbs, Glint-style
/// sparse-impulse diffusors).  A nested private version lives inside
/// `Reverb` for the Freeverb signal chain; this class exists so
/// downstream code can compose its own topology without depending on
/// the Reverb internals.
///
/// Delay is integer samples (no fractional interpolation), matching the
/// standard Schroeder construction; a linear-interpolated variant can
/// be added later if a modulated allpass is needed.
class AllpassFilter
{
public:
    AllpassFilter();

    /// Allocate buffer for up to maxDelaySec seconds.  Calls reset().
    void prepare(float sampleRate, float maxDelaySec = 0.1f);

    /// Zero the delay buffer.
    void reset();

    /// Delay time in seconds.  Clamped to [1 sample, maxDelaySec].
    /// Integer-sample truncation (no fractional interpolation).
    void setDelay(float delaySec);

    /// Diffusion coefficient g.  Clamped to [-0.99, 0.99] so the pole
    /// stays inside the unit circle and the magnitude response stays
    /// flat.  Typical Schroeder values sit around 0.5–0.7.
    void setFeedback(float g);

    /// Process one sample.
    float process(float input);

    int getDelaySamples() const { return delaySamples_; }

private:
    float sampleRate_ = 44100.0f;
    std::vector<float> buffer_;
    int bufferSize_ = 0;
    int writePos_ = 0;
    int maxDelaySamples_ = 0;
    int delaySamples_ = 1;
    float g_ = 0.5f;
};

} // namespace ideath
