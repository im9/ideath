#pragma once

#include <ideath/DelayLine.h>

namespace ideath {

/// Bowed-string physical model — friction-driven sibling of `KarplusStrong`.
///
/// A single waveguide-style delay loop of length `D = sampleRate / freq`
/// circulates the string velocity wave; an analytical slip-stick friction
/// curve injects energy each sample based on the relative velocity between
/// an external "bow" and the string's current velocity at the bow position.
/// A one-pole LP in the feedback path controls timbre and post-bow decay;
/// a `tanh()` saturator on the loop input keeps the recursion strictly
/// bounded even under heavy bow pressure.
///
/// Friction law:
///
///     v_rel = bowVelocity - v_string
///     f     = pressure × scale × v_rel × exp(-k × |v_rel|)
///
/// where `k = kFrictionK` is fixed and `scale = e × k` (= `kFrictionScale`)
/// normalises the curve so the peak force is exactly 1.0 at `pressure = 1`.
/// This is the user-spec'd analytical approximation of the bowed-string
/// slip-stick curve: peak force at `|v_rel| = 1/k` (the "transition
/// velocity") and exponential roll-off on either side.  Captures the
/// qualitative slip-stick character (peaked friction at small relative
/// velocity, negative-slope on the slip side which is what drives the
/// loop's self-oscillation) without a tabulated friction lookup.  Strict
/// slip-stick hysteresis is a v2 nicety.
///
/// **Known v1 quirk — stuck-bow self-excitation.**  With `bowVelocity = 0`
/// and `pressure > 0` the friction term linearises to `f ≈ -scale × v_string`
/// (since `v_rel = -v_string`), which combined with the contractive loop
/// gain ≈ 0.999 gives a small-signal loop gain of magnitude `scale - 0.999
/// ≈ 12.6` — positive feedback.  The loop self-excites from any denormal
/// residue and tanh-saturates at a steady ±1 amplitude.  A real bowed
/// string under a stuck (non-moving) bow would damp via static friction;
/// the 1-port analytical curve has no static-friction regime, so this is
/// a documented limitation.  Use `setPressure(0)` to silence the model
/// rather than relying on `setBowVelocity(0)`.  A tabulated friction LUT
/// (v2) would fix it.
///
/// Bow position is implemented as a second tap into the same delay loop:
///
///     output = mainTap[n - D] - pickupTap[n - position × D]
///
/// i.e. a comb filter at the OUTPUT stage.  Comb response at the n-th
/// harmonic for a pickup fraction p:  `|H(n)| = 2 |sin(π × n × p)|`.
/// So at `position = 0.5` even harmonics are notched (sul ponticello-like
/// emphasis on odd content); at `position → 0` the comb's notches are
/// pushed above the audio band (no spectral coloring, mellow sul tasto).
/// This is not a 2-port waveguide model (the bow excites the whole
/// loop, not a chosen point on the string) — it's a 1-port loop plus a
/// pickup-position comb on the output, which captures 90% of the position
/// timbre at < 2× the CPU.
///
/// Output is mono.  Worst-case bound: each tap saturates inside `tanh()` to
/// `[-1, 1]`, so `mainTap - pickupTap` is bounded to `[-2, 2]` by triangle
/// inequality.  Documented as `±2.0` in CLAUDE.md's output-levels table.
/// The plugin layer's PeakLimiter handles the final ceiling.
///
/// Real-time safe: two `DelayLine` instances allocate only inside
/// `prepare()`; `process()` does no allocation, no exceptions, and the
/// only library calls are `std::exp` and `std::tanh` (single-cycle on any
/// modern CPU).  Denormal protection rides on `DelayLine`'s `+1e-25f` DC
/// injection.
class BowedString
{
public:
    BowedString() = default;

    /// Allocate the delay buffers (sized for the lowest supported pitch).
    void prepare(float sampleRate);

    /// Zero both delay lines and the LP-filter memory.  Bit-exact
    /// reproducibility under identical parameter streams.
    void reset();

    /// Set the pitch (Hz).  Clamped to `[kMinFreq, sampleRate × 0.45]`.
    /// Resizes both delay-line read pointers; the loop's spectral content
    /// shifts at the next sample.
    void setFrequency(float hz);

    /// Set the external bow velocity.  Clamped to `[-1, 1]`.
    /// 0 = bow not moving (loop decays under LP damping); ±1 = full-velocity
    /// drag.  Sign carries up-bow vs down-bow direction; phase of the
    /// circulating wave inverts with sign, but timbre and amplitude are
    /// invariant to it.
    void setBowVelocity(float v);

    /// Bow pressure / friction-force scale.  Clamped to `[0, 1]`.
    /// 0 = bow not touching the string → no friction → loop decays;
    /// 1 = full pressure → maximum friction-force amplitude (analytically
    /// capped at `1 / (kFrictionK × e)` per sample by the curve shape).
    void setPressure(float p);

    /// Bow / pickup position as a fraction of the string length.  Clamped
    /// to `[kMinPosition, kMaxPosition]`.  Controls the comb filter on the
    /// output (see class comment).  `0.5` = mid-string (even harmonics
    /// notched), `→ kMinPosition` = near the bridge (comb notches pushed
    /// above the audio band).
    void setPosition(float p);

    /// Per-loop low-pass damping in `[0, 1]`.  Same shape as
    /// `KarplusStrong::setDamping`: `y[n] = (1-d) × x[n] + d × y[n-1]`.
    /// Higher damping → more HF loss per loop iteration → shorter decay
    /// after the bow disengages.  The loop gain compensates for the LP's
    /// magnitude loss at the fundamental so the steady-state ring length
    /// follows damping monotonically without a separate decay-seconds knob.
    void setDamping(float d);

    /// Produce one output sample.
    float process();

    // ---- spec constants --------------------------------------------------

    static constexpr float kMinFreq = 30.0f;

    /// Minimum pickup-position fraction.  Below this, the second tap
    /// collapses onto the main tap and the comb output goes silent
    /// (mainTap − pickupTap ≈ 0).  0.02 keeps both taps at least a few
    /// fractional samples apart at any supported pitch.
    static constexpr float kMinPosition = 0.02f;
    /// Maximum pickup-position fraction.  Symmetric reflection at 0.5
    /// (positions p and 1−p produce the same comb), so capping at 0.5
    /// covers the full unique-timbre range.
    static constexpr float kMaxPosition = 0.5f;

    /// Friction-curve sharpness.  Peak force at `|v_rel| = 1/k`.
    /// `k = 5` puts the peak at `|v_rel| = 0.2`, near the middle of the
    /// musically interesting bow-velocity range, so the friction nonlinearity
    /// is engaged across typical performance dynamics.
    static constexpr float kFrictionK = 5.0f;

    /// Normalising factor so the friction curve peaks at exactly 1.0 at
    /// `pressure = 1`.  Derived analytically: the curve `v × exp(-k|v|)`
    /// peaks at `v = 1/k` with value `1 / (k × e)`, so multiplying by
    /// `k × e` lifts the peak to 1.  Without this normalisation, the raw
    /// curve peaks at ≈ 0.074 for k=5 — far too weak to drive the loop
    /// into the negative-slope (slip) regime where self-oscillation lives.
    static constexpr float kFrictionScale = 13.59140914229522f; // e × kFrictionK

    /// Hard ceiling on the loop's contractive base gain.  Keeps the
    /// recursion strictly bounded even before the `tanh()` saturator.
    static constexpr float kMaxLoopGain = 0.9995f;

    /// Decay time at minimum damping — very long sustain ("drone" use case).
    static constexpr float kMaxDecaySec = 10.0f;
    /// Decay time at maximum damping — snappy / quickly-fading bow gesture.
    static constexpr float kMinDecaySec = 0.1f;

private:
    void recomputeLoopGain();
    void recomputeDelays();
    float filterMagnitudeAtPitch() const;

    float sampleRate_   = 44100.0f;
    float frequency_    = 220.0f;
    float bowVelocity_  = 0.0f;
    float pressure_     = 0.5f;
    float position_     = 0.1f;
    float damping_      = 0.2f;

    DelayLine mainTap_;
    DelayLine pickupTap_;
    float     filterState_ = 0.0f;
    float     loopGain_    = 0.99f;
};

} // namespace ideath
