#pragma once

#include <ideath/Biquad.h>

namespace ideath {

/// Vactrol / Low-Pass Gate (Buchla Model 292-style).
///
/// A single trigger fires a vactrol envelope that drives BOTH a VCA
/// (amplitude) and a VCF (lowpass cutoff) — the iconic West Coast LPG
/// "ping" character of Don Buchla's 200-series.  The envelope is a fast
/// (~1 ms) exponential rise on trigger and a slow (80 ms – 600 ms,
/// `damping`-controlled) exponential fall, matching the VTL5C-series
/// photoresistor's asymmetric response.
///
/// Signal flow:
///
///     carrier ──► VCF (LP, cutoff driven by envelope) ──► VCA (× envelope) ──► out
///
/// where `cutoff(env)` is an exponential interpolation in Hz between
/// `kClosedCutoff` (envelope = 0) and a peak cutoff (envelope = 1)
/// controlled by `brightness`:
///
///     peakCutoff = kClosedCutoff × (kOpenCutoff / kClosedCutoff)^brightness
///     cutoff(e)  = kClosedCutoff × (peakCutoff / kClosedCutoff)^e
///
/// Exponential (= linear-in-pitch) mapping matches both the perceived
/// brightness sweep AND the actual photoresistor-conductance-vs-current
/// curve being roughly logarithmic.
///
/// This is the v1 acceptance-bar implementation per the request: ~1 ms
/// attack, exponential `damping`-controlled fall, VCA × VCF coupled to
/// the same envelope.  Strict vactrol hysteresis (downward response
/// slower than upward by a separate time constant) is a v2 nicety.
///
/// Output bound: carrier in [-1, 1], LP DC gain = 1 (RBJ LP, Q=0.707
/// peaks at ≈ +1 dB ≈ ×1.12), envelope ∈ [0, velocity], velocity ≤ 1
/// → output bounded to ±1.3.  Listed in CLAUDE.md's output-levels table.
class LowPassGate
{
public:
    LowPassGate() = default;

    /// Allocate filter state and recompute envelope coefficients.
    void prepare(float sampleRate);

    /// Force the envelope to idle and clear the LP filter memory.
    /// User-set parameters (`damping`, `brightness`) are kept; matches
    /// the project-wide reset() convention (KS, BowedString, modal).
    void reset();

    /// Vactrol fall-time scaler.  Clamped to `[0, 1]`.
    /// 0 = `kMinFallSec` (≈ 80 ms — snappy ping);
    /// 1 = `kMaxFallSec` (≈ 600 ms — sustained pluck).
    /// Mapped exponentially (linear in log-time) so the perceived decay
    /// length scales smoothly under modulation.
    void setDamping(float d);

    /// Peak filter cutoff the LPG opens to at envelope = 1.  Clamped to
    /// `[0, 1]`.  0 = `kClosedCutoff` (LPG never opens — fully dark);
    /// 1 = `kOpenCutoff` (LPG opens to ≈ 6 kHz at peak — fully bright).
    /// Exponential mapping (see class comment).
    void setBrightness(float b);

    /// Fire the vactrol envelope.  `velocity` is clamped to `[0, 1]` and
    /// becomes the envelope's target peak (linear scaling).
    /// Calling `trigger()` while the envelope is still ringing simply
    /// restarts attack from the current level (no click).
    void trigger(float velocity = 1.0f);

    /// Process one sample.  `carrier` is the external tonal source
    /// (sine / tri / saw / square / wavetable); the LPG applies VCF + VCA.
    float process(float carrier);

    // ---- introspection (test-only) ---------------------------------------
    float getEnvelope() const { return envelope_; }

    // ---- spec constants --------------------------------------------------

    /// Vactrol LED rise.  ~1 ms is the canonical VTL5C3 / VTL5C9 rise
    /// time and the iconic "snap" of the LPG ping attack.
    static constexpr float kAttackSec = 0.001f;

    /// Fall-time endpoints (decay seconds = log-linear interp on damping).
    static constexpr float kMinFallSec = 0.08f;   // 80 ms — snappy ping
    static constexpr float kMaxFallSec = 0.6f;    // 600 ms — sustained pluck

    /// LPG cutoff endpoints.  50 Hz is below the audible-fundamental
    /// range — at full close the LPG sounds completely dark.  6 kHz
    /// is the canonical open ceiling for the Buchla 292; higher cutoffs
    /// drift into "VCF mode" territory.
    static constexpr float kClosedCutoff = 50.0f;
    static constexpr float kOpenCutoff   = 6000.0f;

    /// LP filter Q.  0.707 is the Butterworth choice — flat passband,
    /// no peaking.  Higher Q would emphasise the cutoff sweep but would
    /// also make the LPG sound less like a vactrol (LPGs don't ring).
    static constexpr float kLpQ = 0.707f;

    /// Flush-to-zero threshold for the decay tail.  Once envelope falls
    /// below this the stage is forced back to Idle, killing any residual
    /// denormal accumulation in the LP filter state.
    static constexpr float kSilenceThreshold = 1e-6f;

private:
    enum class Stage { Idle, Attack, Decay };

    void recomputeCoefs();
    void recomputeCutoffPeakLog();

    float sampleRate_   = 44100.0f;
    float damping_      = 0.5f;
    float brightness_   = 0.5f;

    Stage stage_         = Stage::Idle;
    float envelope_      = 0.0f;
    float triggerLevel_  = 1.0f;

    float attackCoef_       = 0.0f;
    float decayCoef_        = 0.0f;
    float logClosedCutoff_  = 0.0f;
    float logCutoffSpan_    = 0.0f;  // log(peakCutoff) - log(kClosedCutoff)

    Biquad filter_;
};

} // namespace ideath
