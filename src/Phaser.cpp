#include <ideath/Phaser.h>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ideath {

// Denormal DC injection matches the library convention (see CLAUDE.md).
// Applied to the allpass state buffers each sample.
static constexpr float kAntiDenormal = 1e-25f;
static constexpr float kTwoPi        = 6.28318530717958647692f;

// Max sweep in octaves per unit of `depth`.  depth = 1 → ±2 octaves → the
// centre frequency spans a factor of 16 across one LFO cycle.  This is the
// range where a musically-set 800 Hz centre still stays inside [50, 8000]
// Hz — the wide edges of the clamp catch anything overshooting under
// modulation.
static constexpr float kMaxOctavesPerDepth = 2.0f;

void Phaser::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    setRate(rateHz_);  // recompute phaseInc from stored rate
    reset();
}

void Phaser::reset()
{
    prevX_.fill(0.0f);
    prevY_.fill(0.0f);
    phase_   = 0.0f;
    lastOut_ = 0.0f;
}

void Phaser::setStages(int n)
{
    stages_ = std::clamp(n, kMinStages, kMaxStages);
}

void Phaser::setRate(float hz)
{
    // Same LFO range envelope as Chorus — sub-audio only.
    rateHz_   = std::clamp(hz, 0.01f, 20.0f);
    phaseInc_ = rateHz_ / sampleRate_;
}

void Phaser::setDepth(float amount)
{
    depth_ = std::clamp(amount, 0.0f, 1.0f);
}

void Phaser::setCenterHz(float hz)
{
    centerHz_ = std::clamp(hz, 50.0f, 8000.0f);
}

void Phaser::setFeedback(float amount)
{
    feedback_ = std::clamp(amount, -0.95f, 0.95f);
}

void Phaser::setMix(float wet)
{
    mix_ = std::clamp(wet, 0.0f, 1.0f);
}

float Phaser::process(float in)
{
    // 1) Advance LFO and compute the instantaneous per-stage break freq.
    phase_ += phaseInc_;
    phase_ -= std::floor(phase_);
    const float lfo = std::sin(kTwoPi * phase_);  // [-1, 1]

    // Log-frequency sweep: fc = centre × 2^(depth · kMaxOctavesPerDepth · lfo).
    // Clamp the result to a safe range so tan(π · fc / sr) never diverges
    // at Nyquist and never becomes 0 at DC.
    float fc = centerHz_ * std::exp2(depth_ * kMaxOctavesPerDepth * lfo);
    fc = std::clamp(fc, 20.0f, sampleRate_ * 0.45f);

    // 2) First-order allpass coefficient:
    //    H(z) = (a + z^-1) / (1 + a z^-1)
    //    a = (tan(π fc / sr) − 1) / (tan(π fc / sr) + 1)
    //    Places the -90° phase point at fc.  Derivation: bilinear transform
    //    of an analog allpass with pole at ω_c.
    const float t = std::tan(static_cast<float>(M_PI) * fc / sampleRate_);
    const float a = (t - 1.0f) / (t + 1.0f);

    // 3) Feedback: tap the last-stage output back into the cascade input.
    //    Positive feedback accentuates the resonant peaks between notches;
    //    negative feedback accentuates the notches (deeper cut).
    //    tanh on the feedback tap bounds the loop: without it a 6-stage
    //    cascade at fb = 0.9 rings up past ±1.7 under noise excitation.
    //    Analog phasers get this bound for free from op-amp saturation, so
    //    the soft-limit is faithful to the reference topology and keeps
    //    the documented ±1.0 output ceiling.
    float x = in + std::tanh(feedback_ * lastOut_);

    // 4) N allpass stages in series.  Direct-form-I first-order:
    //        y[n] = a x[n] + x[n-1] − a y[n-1]
    //    Single-point DC injection on prevX_ suffices: with silent input
    //    (xn = 0) the recurrence collapses to prevY_[n+1] = eps − a·prevY_[n]
    //    → steady state prevY_ = eps/(1+a) ≈ eps > subnormal.  Same pattern
    //    as Biquad's z1 injection propagating through z2 (see src/Biquad.cpp).
    for (int i = 0; i < stages_; ++i)
    {
        const float xn = x;
        const float y  = a * xn + prevX_[static_cast<size_t>(i)]
                         - a * prevY_[static_cast<size_t>(i)];
        prevX_[static_cast<size_t>(i)] = xn + kAntiDenormal;
        prevY_[static_cast<size_t>(i)] = y;
        x = y;
    }
    lastOut_ = x;

    // 5) Dry/wet mix.
    return (1.0f - mix_) * in + mix_ * x;
}

} // namespace ideath
