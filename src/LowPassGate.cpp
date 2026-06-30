#include <ideath/LowPassGate.h>
#include <algorithm>
#include <cmath>

namespace ideath {

void LowPassGate::prepare(float sampleRate)
{
    sampleRate_ = (sampleRate > 0.0f) ? sampleRate : 44100.0f;
    // Biquad is sample-rate-free at construction — coefficients are
    // recomputed per-sample via setLowpass(freq, q, sr) inside process().
    recomputeCoefs();
    recomputeCutoffPeakLog();
    reset();
}

void LowPassGate::reset()
{
    stage_       = Stage::Idle;
    envelope_    = 0.0f;
    filter_.reset();
}

void LowPassGate::setDamping(float d)
{
    damping_ = std::clamp(d, 0.0f, 1.0f);
    recomputeCoefs();
}

void LowPassGate::setBrightness(float b)
{
    brightness_ = std::clamp(b, 0.0f, 1.0f);
    recomputeCutoffPeakLog();
}

void LowPassGate::trigger(float velocity)
{
    triggerLevel_ = std::clamp(velocity, 0.0f, 1.0f);
    if (triggerLevel_ <= 0.0f)
        return;
    // Re-triggering during decay simply restarts attack from the current
    // envelope level (no jump, no click).  Matches the AdsrEnvelope
    // retrigger convention in the rest of the library.
    stage_ = Stage::Attack;
}

void LowPassGate::recomputeCoefs()
{
    // Attack: single-pole RC toward triggerLevel with τ = kAttackSec.
    //   envelope[n] = envelope[n-1] + α × (triggerLevel − envelope[n-1])
    //   α = 1 − exp(−1 / (kAttackSec × sr))
    attackCoef_ = 1.0f - std::exp(-1.0f / (kAttackSec * sampleRate_));

    // Decay: pure exponential discharge with damping-mapped τ.
    //   Damping maps log-linearly between kMinFallSec and kMaxFallSec so
    //   the perceived decay length is smooth under modulation.
    const float logMin   = std::log(kMinFallSec);
    const float logMax   = std::log(kMaxFallSec);
    const float fallSec  = std::exp(logMin + damping_ * (logMax - logMin));
    decayCoef_ = std::exp(-1.0f / (fallSec * sampleRate_));
}

void LowPassGate::recomputeCutoffPeakLog()
{
    // peakCutoff = kClosedCutoff × (kOpenCutoff / kClosedCutoff)^brightness
    //  ⇒ log peakCutoff = log kClosedCutoff + brightness × (log kOpenCutoff − log kClosedCutoff)
    logClosedCutoff_ = std::log(kClosedCutoff);
    const float logOpen = std::log(kOpenCutoff);
    logCutoffSpan_  = brightness_ * (logOpen - logClosedCutoff_);
}

float LowPassGate::process(float carrier)
{
    // 1. Advance the envelope.
    switch (stage_)
    {
        case Stage::Attack:
        {
            envelope_ += attackCoef_ * (triggerLevel_ - envelope_);
            // Promote to Decay once we're within 1 % of the target (≈ 4.6 τ ≈ 4.6 ms).
            if (envelope_ >= triggerLevel_ * 0.99f)
            {
                envelope_ = triggerLevel_;  // snap to exact target — no infinite tail
                stage_    = Stage::Decay;
            }
            break;
        }
        case Stage::Decay:
        {
            envelope_ *= decayCoef_;
            if (envelope_ < kSilenceThreshold)
            {
                // Flush-to-zero (matches DecayEnvelope / AdsrEnvelope pattern).
                envelope_ = 0.0f;
                stage_    = Stage::Idle;
                filter_.reset();
            }
            break;
        }
        case Stage::Idle:
        default:
            // VCA × 0 → output 0, regardless of carrier.
            return 0.0f;
    }

    // 2. Cutoff = exp(logClosedCutoff + envelope × logCutoffSpan), an
    //    exponential interpolation between kClosedCutoff (env=0) and
    //    peakCutoff (env=1).  At brightness=0 logCutoffSpan_=0 → cutoff
    //    stays at kClosedCutoff regardless of envelope (LPG never opens).
    //
    // NB: `setLowpass` is intentionally called every sample.  A naive
    // `if (cutoffHz != lastCutoff_)` short-circuit would never fire (the
    // envelope is exponentially decaying, so cutoff changes every sample by
    // a non-zero amount), and a "skip if change < threshold" optimisation
    // quantises the cutoff sweep into audible staircase artefacts in the
    // characteristic LPG "ping" gesture.  Measured cost: setLowpass adds
    // ~14 ns/sample on Apple M2 Max — ≈ 25 % of LPG's total ~52 ns budget
    // and < 0.5 % of a 44.1 kHz audio thread, well within margin.
    const float cutoffHz = std::exp(logClosedCutoff_ + envelope_ * logCutoffSpan_);
    filter_.setLowpass(cutoffHz, kLpQ, sampleRate_);

    // 3. VCF then VCA.
    const float filtered = filter_.process(carrier);
    return filtered * envelope_;
}

} // namespace ideath
