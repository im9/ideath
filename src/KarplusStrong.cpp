#include <ideath/KarplusStrong.h>

#include <algorithm>
#include <cmath>

namespace ideath {

namespace {
constexpr float kPi = 3.14159265358979323846f;
} // namespace

void KarplusStrong::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;

    // Size the delay line for the longest period we have to represent:
    //   max period = 1 / kMinFreq seconds. Add a 1-sample headroom so that
    //   fractional reads near the end of the buffer always have an `older`
    //   neighbour available for linear interpolation.
    const float maxDelaySec = 1.0f / kMinFreq + 1.0f / sampleRate_;
    line_.prepare(sampleRate_, maxDelaySec);
    // KS uses the delay line as a pure write/read buffer; feedback math is
    // applied externally so DelayLine's own feedback path stays at zero.
    line_.setFeedback(0.0f);
    line_.setMix(1.0f);

    // Recompute the delay length and loop gain against the new sample rate.
    setFrequency(frequency_);
    reset();
}

void KarplusStrong::reset()
{
    line_.reset();
    noise_.reset();           // restore deterministic noise stream
    filterState_ = 0.0f;
    exciterRemaining_ = 0;
}

void KarplusStrong::setFrequency(float hz)
{
    // Clamp to the supported pitch range. The upper bound is well below
    // Nyquist (0.45 × sr) so the delay length stays >> 1 sample and linear
    // interpolation is meaningful.
    const float maxFreq = sampleRate_ * 0.45f;
    frequency_ = std::clamp(hz, kMinFreq, maxFreq);
    delaySamples_ = sampleRate_ / frequency_;
    // DelayLine's setDelay takes seconds; converting via 1/frequency keeps
    // the fractional delay (linear interp) intact for in-tune playback.
    line_.setDelay(1.0f / frequency_);
    recomputeLoopGain();

    // If a pluck is in flight, re-clamp the remaining burst to the new
    // delay length. pluck() clamps burst ≤ delaySamples_ - 1 against the
    // delay at pluck time; without this re-clamp, raising the pitch
    // (shrinking D) mid-pluck would leave the burst longer than the new
    // delay, causing the burst to overlap itself in the line and push the
    // output past the ±1 nominal bound.
    if (exciterRemaining_ > 0)
    {
        const int maxBurst = std::max(1, static_cast<int>(delaySamples_) - 1);
        exciterRemaining_  = std::min(exciterRemaining_, maxBurst);
    }
}

void KarplusStrong::setDecay(float seconds)
{
    decaySeconds_ = std::clamp(seconds, kMinDecay, kMaxDecay);
    recomputeLoopGain();
}

void KarplusStrong::setDamping(float amount)
{
    damping_ = std::clamp(amount, 0.0f, 1.0f);
    recomputeLoopGain();
}

void KarplusStrong::setExciter(float amount)
{
    exciterLevel_ = std::clamp(amount, 0.0f, 1.0f);
}

void KarplusStrong::pluck()
{
    // Burst length in samples. Clamp to (delaySamples_ - 1) so the burst
    // window never overlaps itself: each delay-line slot is written at most
    // once per pluck. Without this clamp, at f0 > 1/kExciterSec ≈ 1 kHz the
    // wavelength is shorter than the 1 ms burst, and the burst would
    // additively rewrite the same slot at t and t+D, producing line values
    // up to ±(1 + g) ≈ ±2 in magnitude — breaking the ±1 nominal output bound
    // promised by the API.
    //
    // For low pitches (period > 1 ms) the burst stays at the canonical 1 ms.
    // For high pitches the burst shrinks to one period — still long enough
    // to inject broadband noise covering the loop's first traversal, which is
    // the only purpose of the burst.
    const int wanted   = static_cast<int>(kExciterSec * sampleRate_);
    const int maxBurst = std::max(1, static_cast<int>(delaySamples_) - 1);
    exciterRemaining_  = std::max(1, std::min(wanted, maxBurst));
}

float KarplusStrong::filterMagnitudeAtPitch() const
{
    // One-pole LP:   y[n] = (1-d) x[n] + d y[n-1]
    // Transfer fn:   H(z) = (1-d) / (1 - d z^-1)
    // |H(e^jω)|^2 = (1-d)^2 / (1 - 2 d cos ω + d^2)
    // Evaluated at the loop fundamental ω = 2π f0 / fs.
    const float omega = 2.0f * kPi * frequency_ / sampleRate_;
    const float d = damping_;
    const float num   = (1.0f - d);
    const float denom = std::sqrt(1.0f - 2.0f * d * std::cos(omega) + d * d);
    // denom is bounded below by |1 - d| > 0 for d ∈ [0, 1) — but at d == 1
    // exactly the formula degenerates (pole on unit circle, DC gain only).
    // Guard with a small floor so the division never produces inf/NaN; the
    // resulting loop gain will then be clamped by kMaxLoopGain anyway.
    return num / std::max(denom, 1e-6f);
}

void KarplusStrong::recomputeLoopGain()
{
    // Cycles to reach -60 dB at the target decay time. A pluck rings the
    // loop once every `delaySamples_` samples, so the number of cycles in
    // `decaySeconds_` is decaySeconds_ * (sampleRate_ / delaySamples_)
    //                = decaySeconds_ * frequency_.
    const float cycles = std::max(1.0f, decaySeconds_ * frequency_);

    // -60 dB per the decay convention used everywhere in this library
    // (AdsrEnvelope release, Compressor envelope follower, etc.).
    //   g_raw^cycles = 10^(-3)  →  g_raw = 10^(-3 / cycles).
    const float gRaw = std::pow(10.0f, -3.0f / cycles);

    // Compensate for the in-loop filter's magnitude loss at the fundamental.
    // Without this, more damping silently shortens the decay; the spec calls
    // out this compensation as required behaviour.
    const float H = filterMagnitudeAtPitch();
    const float g = (H > 1e-6f) ? (gRaw / H) : kMaxLoopGain;

    // Hard ceiling. A sufficiently long decay + heavy damping combination
    // can push g above 1; that would diverge. Clamping to < 1 keeps the
    // loop strictly contractive — the worst case is "tail slightly shorter
    // than asked for", never instability.
    loopGain_ = std::min(g, kMaxLoopGain);
}

float KarplusStrong::process()
{
    // 1. Read the current delay-line output (no write yet).
    const float wet = line_.readDelay();

    // 2. One-pole LP inside the feedback path.
    //    y[n] = (1-d) * x[n] + d * y[n-1]
    //    At d=0 the loop is bright (filter is a pass-through);
    //    at d→1 the loop is dark (long averaging window).
    filterState_ = (1.0f - damping_) * wet + damping_ * filterState_;
    const float filtered = filterState_;

    // 3. Apply loop gain (already compensated for filter loss at f0).
    float feedback = filtered * loopGain_;

    // 4. Inject the exciter burst (additive, so re-plucks layer on top
    //    of any existing tail instead of erasing it).
    if (exciterRemaining_ > 0)
    {
        feedback += noise_.process() * exciterLevel_;
        --exciterRemaining_;
    }

    // 5. Write the new sample into the line. DelayLine::process applies its
    //    own `+ 1e-25f` DC offset for denormal protection, so we get the
    //    same anti-denormal guarantee as every other feedback primitive
    //    in the library without re-implementing it here.
    line_.process(feedback);

    // 6. Output is the pre-filter delay-line tap. The LP filter stays in
    //    the feedback path (shaping each loop iteration's HF content) but
    //    is not on the output: returning `filterState_` would be silent at
    //    damping=1 (filter freezes at zero, the burst would be inaudible).
    //    `wet` is the canonical KS output tap used by Jaffe-Smith 1983
    //    and every standard implementation since.
    return wet;
}

} // namespace ideath
