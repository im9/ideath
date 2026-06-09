#pragma once

#include <ideath/DelayLine.h>
#include <ideath/Noise.h>

namespace ideath {

/// Karplus-Strong plucked-string synthesis.
///
/// A short noise burst is written into a circular delay line whose length
/// equals one wavelength of the desired pitch. The delay output is low-pass
/// filtered and fed back into the line; each loop iteration loses energy
/// (controlled by `setDecay`) and high-frequency content (controlled by
/// `setDamping`), producing the characteristic plucked-string decay.
///
/// Loop math:
///   D       = sampleRate / freq         (delay length, fractional)
///   N_cyc   = decay_seconds * freq      (number of loop cycles to reach -60 dB)
///   g_raw   = 10^(-3 / N_cyc)           (per-cycle gain for -60 dB at decay)
///   H(f0)   = filter magnitude at pitch (≤ 1; depends on damping)
///   g       = g_raw / H(f0)             (compensate so the tail still hits
///                                        -60 dB at the target time even when
///                                        damping costs gain inside the loop)
///
/// `g` is clamped to a hard ceiling of 0.9995 so an aggressive (high damping)
/// + (very long decay) combination cannot push the loop into runaway.
///
/// Output is mono, nominal range ±1.0 (the exciter is ±exciterAmount, and
/// the loop gain never exceeds 1, so the steady-state envelope is monotonic
/// downward).
class KarplusStrong
{
public:
    KarplusStrong() = default;

    /// Allocate the delay line. Max delay sized for the lowest supported pitch
    /// (kMinFreq).
    void prepare(float sampleRate);

    /// Zero the delay line, filter state, and pending-exciter counter.
    void reset();

    /// Set the pitch in Hz. Clamped to [kMinFreq, sampleRate * 0.45].
    /// Internally sets the delay length to sampleRate / freq.
    void setFrequency(float hz);

    /// Set the -60 dB tail length in seconds.
    /// Clamped to [kMinDecay, kMaxDecay].
    void setDecay(float seconds);

    /// Set the per-loop low-pass damping. Clamped to [0, 1].
    ///   0   → no damping (one-pole at Nyquist, bright)
    ///   1   → maximum damping (filter pole → near 1, very dark, fast HF loss)
    /// Implementation: y[n] = (1-d)*x[n] + d*y[n-1].
    void setDamping(float amount);

    /// Set the noise-burst level used by pluck(). Clamped to [0, 1].
    void setExciter(float amount);

    /// Fire the exciter: schedule writing the next ~1 ms of process() calls
    /// with ±exciter-level noise added on top of the feedback signal. Calling
    /// pluck() again while the previous burst is still in flight simply
    /// replaces the remaining count (a fresh pluck restarts the burst).
    void pluck();

    /// Process one sample. No input — KS is a self-contained source.
    float process();

    // ---- introspection (test-only, no setters here are needed at runtime) --
    float getDelaySamples() const { return delaySamples_; }
    bool exciterActive() const   { return exciterRemaining_ > 0; }

    // ---- spec constants ---------------------------------------------------
    static constexpr float kMinFreq   = 30.0f;
    static constexpr float kMaxDecay  = 5.0f;
    static constexpr float kMinDecay  = 0.05f;
    /// Exciter burst length in seconds. ~1 ms is the canonical KS choice:
    /// short enough to read as a transient, long enough that the burst's
    /// own bandwidth covers all of the loop's first traversal.
    static constexpr float kExciterSec = 0.001f;
    /// Hard ceiling on the per-loop gain. Below 1.0 guarantees stability
    /// even when damping compensation pushes g_raw close to unity.
    static constexpr float kMaxLoopGain = 0.9995f;

private:
    float sampleRate_      = 44100.0f;
    float frequency_       = 440.0f;
    float decaySeconds_    = 1.0f;
    float damping_         = 0.5f;
    float exciterLevel_    = 1.0f;

    float delaySamples_    = 100.0f;   // sampleRate / frequency
    float loopGain_        = 0.9f;     // g (compensated for filter loss)

    DelayLine line_;
    Noise     noise_;
    float     filterState_ = 0.0f;     // one-pole LP memory

    int exciterRemaining_  = 0;        // samples of burst left to write

    /// Recompute loopGain_ from frequency_, decaySeconds_, damping_.
    /// Called from every setter that touches one of those three quantities.
    void recomputeLoopGain();

    /// Magnitude of the one-pole LP `y = (1-d)*x + d*y[-1]` at the
    /// fundamental ω = 2π * frequency_ / sampleRate_.
    float filterMagnitudeAtPitch() const;
};

} // namespace ideath
