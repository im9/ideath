#include <ideath/KarplusStrong.h>

#include <algorithm>
#include <cmath>

namespace ideath {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr double kPiD = 3.14159265358979323846;
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
    dcRemaining_ = 0;
}

void KarplusStrong::setFrequency(float hz)
{
    // Clamp to the supported pitch range. The upper bound is well below
    // Nyquist (0.45 × sr) so the delay length stays >> 1 sample and linear
    // interpolation is meaningful.
    const float maxFreq = sampleRate_ * 0.45f;
    const float clamped = std::clamp(hz, kMinFreq, maxFreq);
    // Hosts call the setters per sample under modulation; an unchanged value
    // skips the trig in recomputeLoopDelay / recomputeLoopGain.
    if (clamped == frequency_ && sampleRate_ / frequency_ == delaySamples_)
        return;
    frequency_ = clamped;
    delaySamples_ = sampleRate_ / frequency_;
    recomputeLoopDelay();
    recomputeLoopGain();
}

void KarplusStrong::setDecay(float seconds)
{
    const float clamped = std::clamp(seconds, kMinDecay, kMaxDecay);
    if (clamped == decaySeconds_)
        return;
    decaySeconds_ = clamped;
    recomputeLoopGain();
}

void KarplusStrong::setDamping(float amount)
{
    const float clamped = std::clamp(amount, 0.0f, 1.0f);
    if (clamped == damping_)
        return;
    damping_ = clamped;
    recomputeLoopDelay();
    recomputeLoopGain();
}

void KarplusStrong::setExciter(float amount)
{
    exciterLevel_ = std::clamp(amount, 0.0f, 1.0f);
}

void KarplusStrong::pluck()
{
    // Burst length in samples. Clamp to (line delay - 1) so the burst
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
    const int maxBurst = std::max(1, static_cast<int>(lineDelaySamples_) - 1);
    const double written = exciterWritten();
    exciterRemaining_  = std::max(1, std::min(wanted, maxBurst));
    dcRemaining_       = maxBurst;
    dcElapsed_         = 0;
    planExciter(written);
}

double KarplusStrong::exciterWritten() const
{
    // Each plan fixes the exciter's total, (written so far) + (still to
    // write) = planTotal_ — zero, except where planExciter() could not
    // cancel everything. So what has been written is the total minus what
    // the current plan still has to write.
    if (dcRemaining_ <= 0)
        return 0.0;
    Noise probe = noise_;
    double rest = static_cast<double>(dcStep_) * dcRemaining_;
    for (int i = 0; i < exciterRemaining_; ++i)
        rest += static_cast<double>(burstScale_) * probe.process();
    return planTotal_ - rest;
}

void KarplusStrong::planExciter(double written)
{
    // Why: the loop (line → LP → ×g) passes DC with gain g — the LP costs
    // nothing at DC while g is raised to make up its loss at f0 — so the
    // loop's DC mode decays slower than the fundamental (at the
    // kMaxLoopGain ceiling, ≈ 13 800 periods to −60 dB). Whatever the
    // exciter's samples sum to stays in the loop as that mode: a burst of N
    // uniform ±1 samples sums to ≈ 0.58·√N, and nothing in a string's
    // velocity wave is DC.
    //
    // Why not just remove the burst's mean: with the burst much shorter
    // than the period (1 ms against 15 ms at C2), its spectrum is flat from
    // DC up past the low partials — the sum that feeds the DC mode is the
    // same number that feeds the fundamental. A zero-mean burst loses the
    // fundamental with the DC (≈ −22 dB at C2).
    //
    // Instead the correction is a constant −S/M over the M = int(line) − 1
    // samples after the pluck (one loop length, less the sample the
    // interpolated read needs; M never overlaps itself in the line). It
    // cancels the sum S exactly, so the DC mode gets nothing, while a
    // constant across a full period has no component at k·f0 — the
    // harmonics keep their excitation, short by the (P − M)/P of a period
    // the window misses (a few samples: < 1 % at C2, where it matters).
    //
    // Re-plans (a re-pluck in flight, a pitch or damping change while the
    // exciter runs) carry what was already written, W, into the new
    // sum. The exciter level is taken here, at pluck time (a setExciter()
    // while the exciter is in flight applies to the next pluck), so the
    // plan, and W derived from it, are in output units. The noise gain s is
    // the level, lowered only if noise + correction could pass ±1 — the
    // output bound:
    //   step = −(W + s·S) / M,   s·max|r| + s·|S|/M + |W|/M ≤ 1.
    // |W|/M ≥ 1 cannot be met with any noise left (a cut-short window of a
    // couple of samples after a jump of several octaves): the correction is
    // then clamped to ±1 and the rest of W stays in the loop. Either way
    // planTotal_ records what this plan leaves uncancelled, so a later
    // re-plan still knows what was written.
    const int M = dcRemaining_;
    Noise probe = noise_;
    double sum = 0.0, peak = 0.0;
    for (int i = 0; i < exciterRemaining_; ++i)
    {
        const double r = probe.process();
        sum += r;
        peak = std::max(peak, std::fabs(r));
    }
    const double level = exciterLevel_;
    if (M < 2)
    {
        // A one-sample window (pitch above ≈ sr/3) cannot hold a burst and
        // its opposite: the burst goes in as drawn, and neither its sum nor
        // the W of an earlier plan is cancelled.
        burstScale_ = static_cast<float>(level);
        dcStep_ = 0.0f;
        planTotal_ = written + static_cast<double>(burstScale_) * sum;
        return;
    }
    const double w = std::fabs(written) / M;
    if (w >= 1.0)
    {
        burstScale_ = 0.0f;
        dcStep_ = static_cast<float>(std::clamp(-written / M, -1.0, 1.0));
        planTotal_ = written + static_cast<double>(dcStep_) * M;
        return;
    }
    const double need = peak + std::fabs(sum) / M;
    const double s = (need * level > 1.0 - w) ? (1.0 - w) / need : level;
    burstScale_ = static_cast<float>(s);
    dcStep_ = static_cast<float>(-(written + s * sum) / M);
    // Zero up to the float rounding of the two stored values.
    planTotal_ = written + static_cast<double>(burstScale_) * sum
               + static_cast<double>(dcStep_) * M;
}

void KarplusStrong::recomputeLoopDelay()
{
    // The loop is: delay line (linear-interpolated read) → one-pole LP →
    // back into the line. Reading before writing in process() adds no
    // extra sample, so the loop delay at the fundamental is
    //
    //   lineDelay + τ_LP(ω0) = one period P = sr / f0,
    //
    // where τ_LP(ω) = atan(d·sin ω / (1 − d·cos ω)) / ω is the one-pole's
    // phase delay. A line delay of exactly P would leave the loop
    // τ_LP samples too long and the string rings flat — by up to ~50 cents
    // at C6 with damping 0.5.
    //
    // The fractional part of the line delay goes through linear
    // interpolation, whose phase delay at ω0 is not the fraction itself
    // but atan(f·sin ω / (1 − f + f·cos ω)) / ω. Solving that for the
    // fraction f that gives the remaining phase θ = ω0·(L − Dint) exactly:
    //
    //   f = sin θ / (sin(ω0 − θ) + sin θ),   0 ≤ θ < ω0 < π.
    //
    // The line delay in samples is Dint + f. τ_LP < P/4 for every d in
    // [0, 1] (at d = 1 it is (π − ω0) / (2ω0)), so L = P − τ_LP stays above
    // 1.5π/ω0 > 1 sample across the whole clamped pitch range, and the
    // line never has to be shorter than the one sample DelayLine needs.
    const double w  = 2.0 * kPiD * static_cast<double>(frequency_)
                    / static_cast<double>(sampleRate_);
    const double d  = static_cast<double>(damping_);
    const double tauLp = std::atan2(d * std::sin(w), 1.0 - d * std::cos(w)) / w;
    const double L     = std::max(1.0, static_cast<double>(delaySamples_) - tauLp);
    const double whole = std::floor(L);
    const double theta = w * (L - whole);
    const double frac  = std::sin(theta) / (std::sin(w - theta) + std::sin(theta));
    lineDelaySamples_  = static_cast<float>(whole + frac);
    lineFrac_          = static_cast<float>(frac);
    line_.setDelay(lineDelaySamples_ / sampleRate_);

    // A shorter line (higher pitch or more damping) can leave a pluck in
    // flight with more burst to write than the line holds. pluck() clamps
    // the burst to (line − 1) at pluck time; re-clamp here so the burst
    // never overlaps itself in the line, which would push the output past
    // the ±1 nominal bound.
    //
    // The DC correction window follows the line too: it should span one
    // loop length from the pluck at the pitch the string now has. A host
    // that plucks first and sets the new note's pitch after (the REPL's
    // per-sample setters, tristero's per-sample pitch) would otherwise keep
    // a window sized to the previous note — on a pitch drop a constant
    // shorter than the period, which does carry energy at the low
    // harmonics and cancels part of the fundamental. So on any change in
    // flight the window becomes (line − 1) − elapsed samples still to
    // write, re-planned to still cancel what the exciter has written. All
    // writes then land within (line − 1) samples of the pluck, before the
    // first of them comes back round, so the exciter never overlaps itself
    // (the ±1 bound). If the line has shrunk below what is already written
    // (a jump of an octave or more late in the window), the exciter ends
    // there and what it wrote stays in the loop. exciterRemaining_ ≤
    // dcRemaining_ holds throughout (both start at ≤ line − 1, the burst no
    // longer).
    // Re-planning reads ahead only over the burst still to write, so once
    // the 1 ms burst is out it is O(1) — cheap under per-sample vibrato.
    if (dcRemaining_ > 0)
    {
        const int maxBurst = std::max(1, static_cast<int>(lineDelaySamples_) - 1);
        const int target   = std::max(0, maxBurst - dcElapsed_);
        if (target != dcRemaining_)
        {
            const double written = exciterWritten();
            dcRemaining_      = target;
            exciterRemaining_ = std::min(exciterRemaining_, target);
            if (target > 0)
                planExciter(written);
        }
    }
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

    // Compensate for the in-loop filter's magnitude loss at the fundamental,
    // and for the linear interpolator's (|1 − f + f·e^−jω0| < 1 for a
    // fractional line; the fraction depends on damping since the line is
    // shortened by the LP's phase delay). Without this, more damping — or a
    // fraction near ½ at high pitch — silently shortens the decay; the spec
    // calls out this compensation as required behaviour.
    const float omega = 2.0f * kPi * frequency_ / sampleRate_;
    const float a = lineFrac_;
    const float interpMag = std::sqrt((1.0f - a) * (1.0f - a) + a * a
                                      + 2.0f * a * (1.0f - a) * std::cos(omega));
    const float H = filterMagnitudeAtPitch() * interpMag;
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

    // 4. Inject the exciter (additive, so re-plucks layer on top of any
    //    existing tail instead of erasing it): the noise burst plus the DC
    //    correction spread over one loop length (see planExciter()).
    //    The burst lies inside the correction window (exciterRemaining_ ≤
    //    dcRemaining_), so one test covers both.
    if (dcRemaining_ > 0)
    {
        float v = dcStep_;
        if (exciterRemaining_ > 0)
        {
            v += burstScale_ * noise_.process();
            --exciterRemaining_;
        }
        --dcRemaining_;
        ++dcElapsed_;
        feedback += v;
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
