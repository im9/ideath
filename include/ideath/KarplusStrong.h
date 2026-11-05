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
///   P       = sampleRate / freq         (one period, samples)
///   D       = P − τ_LP(ω0)              (delay length, fractional; the
///                                        one-pole's phase delay at the
///                                        fundamental is taken out so the
///                                        loop as a whole is one period)
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
/// The exciter puts no DC into the loop: the loop passes DC (line + LP,
/// DC gain g), so any offset would ring on below the audio band — which a
/// string's velocity wave does not carry. The noise burst is left as drawn
/// (its sum is also what excites the low partials when the burst is short
/// against the period), and the opposite of that sum is spread as a small
/// constant over one loop length. A constant over a whole period has no
/// component at the harmonics, so only the DC mode loses its excitation.
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
    /// The delay line is set one period minus the in-loop LP's phase delay
    /// at the fundamental, so the whole loop is exactly one period long
    /// (recomputed whenever the pitch or the damping changes).
    void setFrequency(float hz);

    /// Set the -60 dB tail length in seconds.
    /// Clamped to [kMinDecay, kMaxDecay].
    void setDecay(float seconds);

    /// Set the per-loop low-pass damping. Clamped to [0, 1].
    ///   0   → no damping (one-pole at Nyquist, bright)
    ///   1   → maximum damping (filter pole → near 1, very dark, fast HF loss)
    /// Implementation: y[n] = (1-d)*x[n] + d*y[n-1].
    void setDamping(float amount);

    /// Set the noise-burst level used by pluck(). Clamped to [0, 1]. Taken
    /// at pluck() time: an exciter already in flight keeps its level.
    void setExciter(float amount);

    /// Fire the exciter: schedule writing the next ~1 ms of process() calls
    /// with ±exciter-level noise added on top of the feedback signal, plus
    /// the DC correction over the next loop length. Calling pluck() again
    /// while the previous burst is still in flight simply replaces the
    /// remaining count (a fresh pluck restarts the burst); the new
    /// correction also cancels what the cut-short exciter wrote.
    void pluck();

    /// Process one sample. No input — KS is a self-contained source.
    float process();

    // ---- introspection (test-only, no setters here are needed at runtime) --
    /// One period in samples (sampleRate / freq).
    float getDelaySamples() const { return delaySamples_; }
    /// Delay-line length in samples: one period minus the LP phase delay.
    float getLineDelaySamples() const { return lineDelaySamples_; }
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

    float delaySamples_    = 100.0f;   // sampleRate / frequency (one period)
    float lineDelaySamples_ = 100.0f;  // period − LP phase delay at f0
    float lineFrac_         = 0.0f;    // interpolation fraction of the line
    float loopGain_        = 0.9f;     // g (compensated for filter loss)

    DelayLine line_;
    Noise     noise_;
    float     filterState_ = 0.0f;     // one-pole LP memory

    int exciterRemaining_  = 0;        // samples of burst left to write

    // DC correction: a constant `dcStep_` added for `dcRemaining_` samples
    // (one loop length from the pluck) so that everything the exciter writes
    // sums to zero. `burstScale_` is the exciter level (taken at pluck()),
    // lowered only when noise + correction would pass ±1. Both in output
    // units; planned at pluck() and re-planned when the line changes while
    // the exciter is in flight (the window then spans one new loop length
    // from the pluck).
    // Invariant: exciterRemaining_ ≤ dcRemaining_ (the burst lies inside
    // the correction window).
    int    dcRemaining_    = 0;
    int    dcElapsed_      = 0;        // samples since the pluck
    double planTotal_      = 0.0;      // what the current plan leaves uncancelled
    float  dcStep_         = 0.0f;
    float  burstScale_     = 1.0f;

    /// Plan the remaining noise and correction so that, with `written` (what
    /// this exciter has already put into the loop), everything sums to zero
    /// and each sample stays within ±1. Reads ahead on a copy of the noise
    /// generator (the stream itself is not advanced).
    void planExciter(double written);

    /// Sum of what the exciter in flight has written so far (0 when none is
    /// in flight). Derived from the current plan, which makes written +
    /// still-to-write = 0, so process() keeps no running sum.
    double exciterWritten() const;

    /// Recompute loopGain_ from frequency_, decaySeconds_, damping_.
    /// Called from every setter that touches one of those three quantities.
    void recomputeLoopGain();

    /// Recompute the delay-line length from frequency_ and damping_ so the
    /// loop (line + LP) is exactly one period at the fundamental.
    void recomputeLoopDelay();

    /// Magnitude of the one-pole LP `y = (1-d)*x + d*y[-1]` at the
    /// fundamental ω = 2π * frequency_ / sampleRate_.
    float filterMagnitudeAtPitch() const;
};

} // namespace ideath
