#include <ideath/BowedString.h>
#include <algorithm>
#include <cmath>

namespace ideath {

namespace {
constexpr float kPi = 3.14159265358979323846f;
} // namespace

void BowedString::prepare(float sampleRate)
{
    sampleRate_ = (sampleRate > 0.0f) ? sampleRate : 44100.0f;

    // Size both delay buffers for the lowest supported pitch (= longest
    // period).  Add a 1-sample headroom for the fractional-delay linear
    // interpolation at the end of the buffer.
    const float maxDelaySec = 1.0f / kMinFreq + 1.0f / sampleRate_;
    mainTap_.prepare(sampleRate_, maxDelaySec);
    pickupTap_.prepare(sampleRate_, maxDelaySec);

    // We manage feedback / mix externally — both delay lines behave as
    // pure write/read buffers here.
    mainTap_.setFeedback(0.0f);
    mainTap_.setMix(1.0f);
    pickupTap_.setFeedback(0.0f);
    pickupTap_.setMix(1.0f);

    reset();
    recomputeDelays();
    recomputeLoopGain();
}

void BowedString::reset()
{
    mainTap_.reset();
    pickupTap_.reset();
    filterState_ = 0.0f;
}

void BowedString::setFrequency(float hz)
{
    const float clamped = std::clamp(hz, kMinFreq, sampleRate_ * 0.45f);
    if (clamped == frequency_)
        return;
    frequency_ = clamped;
    recomputeDelays();
    recomputeLoopGain();
}

void BowedString::setBowVelocity(float v)
{
    bowVelocity_ = std::clamp(v, -1.0f, 1.0f);
}

void BowedString::setPressure(float p)
{
    pressure_ = std::clamp(p, 0.0f, 1.0f);
}

void BowedString::setPosition(float p)
{
    const float clamped = std::clamp(p, kMinPosition, kMaxPosition);
    if (clamped == position_)
        return;
    position_ = clamped;
    recomputeDelays();
}

void BowedString::setDamping(float d)
{
    damping_ = std::clamp(d, 0.0f, 1.0f);
    recomputeLoopGain();
}

void BowedString::recomputeDelays()
{
    // Main tap reads at one full period: y[n - D].
    mainTap_.setDelay(1.0f / frequency_);
    // Pickup tap reads at position × D: y[n - position × D].
    pickupTap_.setDelay(position_ / frequency_);
}

float BowedString::filterMagnitudeAtPitch() const
{
    // One-pole LP `y = (1-d) x + d y[-1]` has |H(e^jω)| =
    // (1-d) / √(1 - 2d cosω + d²).  Evaluated at the loop fundamental.
    // Same closed form as KarplusStrong::filterMagnitudeAtPitch().
    const float omega = 2.0f * kPi * frequency_ / sampleRate_;
    const float d     = damping_;
    const float num   = 1.0f - d;
    const float denom = std::sqrt(1.0f - 2.0f * d * std::cos(omega) + d * d);
    return num / std::max(denom, 1e-6f);
}

void BowedString::recomputeLoopGain()
{
    // Damping interpolates between two endpoints of the post-bow decay
    // time: kMaxDecaySec at damping=0 (drone sustain) → kMinDecaySec at
    // damping=1 (snappy gesture).  Same KS-style closed-form mapping
    // from decay seconds to per-loop gain:
    //   cycles = decay_seconds × frequency  (loop traversals to −60 dB)
    //   g_raw  = 10^(-3 / cycles)           (per-cycle gain)
    //   g      = g_raw / H(f0)              (compensate LP magnitude loss)
    // The LP coefficient `damping_` is reused as-is in the filter step;
    // here we just account for its f0 magnitude loss in the gain budget.
    const float decaySec = (1.0f - damping_) * kMaxDecaySec
                         + damping_         * kMinDecaySec;
    const float cycles   = std::max(1.0f, decaySec * frequency_);
    const float gRaw     = std::pow(10.0f, -3.0f / cycles);
    const float H        = filterMagnitudeAtPitch();
    const float g        = (H > 1e-6f) ? (gRaw / H) : kMaxLoopGain;
    loopGain_            = std::min(g, kMaxLoopGain);
}

float BowedString::process()
{
    // 1. Read both taps from the loop's history.
    const float vString = mainTap_.readDelay();    // y[n - D]
    const float pickup  = pickupTap_.readDelay();  // y[n - position × D]

    // 2. Analytical slip-stick friction.  Peak force at |v_rel| = 1/k
    //    (see class comment); zero force at v_rel = 0 and asymptotic
    //    decay as |v_rel| → ∞ (full slip).  The kFrictionScale factor
    //    normalises the curve peak to 1.0 at pressure=1 so the friction
    //    is strong enough to push the loop into the negative-slope
    //    region of the curve (where self-oscillation lives).
    const float vRel    = bowVelocity_ - vString;
    const float absVRel = std::fabs(vRel);
    const float fricForce = pressure_ * kFrictionScale * vRel
                          * std::exp(-kFrictionK * absVRel);

    // 3. Loop input = contractive recirculation + friction injection.
    //    tanh() bounds the loop strictly to [-1, 1] inside the delay line;
    //    keeps the recursion stable under any (pressure × loopGain) combo.
    const float loopInput = std::tanh(vString * loopGain_ + fricForce);

    // 4. Low-pass in the feedback path (timbre + decay).
    filterState_ = (1.0f - damping_) * loopInput + damping_ * filterState_;

    // 5. Write the post-filter value into BOTH delay lines so the two
    //    taps share the same circulating wave.
    mainTap_.process(filterState_);
    pickupTap_.process(filterState_);

    // 6. Output = comb on the historical loop content.  See class comment
    //    for the analytical comb response.
    return vString - pickup;
}

} // namespace ideath
