#pragma once

#include <array>

namespace ideath {

/// Anti-aliasing mode for memoryless nonlinearities (Wavefolder,
/// Saturation::AntialiasedTanh).
///
/// - Off:    the plain per-sample function at 1× (aliases; no latency).
/// - Adaa:   first-order antiderivative anti-aliasing at 1×.  The output is
///           the mean of f over the straight line between the previous and
///           the current input, i.e. (F(x) − F(x₁)) / (x − x₁).  Measured
///           8–12 dB less alias (band-limited saw 880–3520 Hz, wavefolder
///           drive 2–10, tanh drive 3–10); costs a half-sample delay and the
///           two-point-mean roll-off |H| = cos(π·f / sr) (−3 dB at sr/4,
///           −11.7 dB at 20 kHz / 48 kHz).
/// - Adaa2x: the same ADAA run at 2× between two linear-phase halfband FIRs
///           (47 taps, Kaiser β = 8).  Measured 15–22 dB less alias than Off
///           (same cases); roll-off cos(π·f / 2sr) plus the halfband pair:
///           −0.5 dB at 10 kHz, −2.2 dB at 20 kHz / 48 kHz (−2.0 dB mean,
///           −0.2 dB filters, whose pass band ends ≈ 18.7 kHz at 48 kHz);
///           22.75-sample delay.
///
/// Switching mode resets the anti-aliasing state (the delay changes, so the
/// switch itself is not click-free — choose the mode before playback).
enum class Antialiasing
{
    Off,
    Adaa,
    Adaa2x,
};

namespace detail {

/// 2× up/down sampler built from one linear-phase halfband FIR.
///
/// Taps h[j], j = 0 … 46, centre 23: h[23] = 1/2, h[23 ± 2k] = 0, and the 24
/// even-index taps g[i] = h[2i] (Kaiser-windowed sinc, β = 8: ≈ −80 dB stop
/// band from ≈ 29 kHz at 2 × 48 kHz, pass-band ripple ≈ 1e−4) normalised so
/// Σg = 1/2 (unity DC through each polyphase branch).
///
/// upsample: zero-stuff by 2 and filter with gain 2 → even output
///   2·Σ g[i]·x[n−i], odd output x[n−11] (the centre tap).
/// downsample: filter and keep one sample in two →
///   y[n] = Σ g[i]·u_odd[n−i] + ½·u_even[n−11].
/// Each pass delays by 23 samples at 2×; the pair delays by 22.5 base
/// samples (the odd-phase pick saves half a base sample).
class Halfband2x
{
public:
    static constexpr int kTaps = 47;
    static constexpr int kDense = 24;           // non-zero off-centre taps
    static constexpr int kCentreDelay = 11;     // base-rate samples
    /// Base-rate delay of upsample → downsample with nothing between.
    static constexpr float kLatency = 22.5f;

    /// The full 47-tap filter (h[j]), computed once.
    static const std::array<float, kTaps>& taps();

    Halfband2x();

    void reset();
    void upsample(float x, float& even, float& odd);
    float downsample(float even, float odd);

private:
    // Newest-first histories, each written twice (at i and i + N) so a
    // contiguous window of N samples is always readable from the index.
    std::array<float, 2 * kDense> upHist_{};
    std::array<float, 2 * kDense> oddHist_{};
    std::array<float, 2 * (kCentreDelay + 1)> evenHist_{};
    const float* g_ = nullptr; // the 24 dense taps (shared, bound at construction)
    int upIdx_ = 0;
    int oddIdx_ = 0;
    int evenIdx_ = 0;
};

} // namespace detail
} // namespace ideath
