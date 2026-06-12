#pragma once

#include <array>
#include <cstdint>

namespace ideath {

/// Plaits-style additive harmonic oscillator.
///
/// Sums up to `kMaxPartials` (32) sine waves at integer multiples of a single
/// fundamental frequency.  Partial `i` (0-indexed) has frequency
/// `fundamental × (i + 1)`, individual amplitude in `[0, 1]`, and an
/// independent phase accumulator.  Partials whose effective frequency exceeds
/// `sampleRate × 0.45` are silently muted (no aliasing fold).
///
/// This is the sustained-tonal core of slothrop's Loom engine — modelled on
/// MI Plaits Model 5 "An additive mixture of harmonically-related sine waves".
///
/// Two amplitude control surfaces are exposed:
///
///   - **High level** (`setBands`) maps three Plaits-style band scalars
///     (LOW = partials 1..3, MID = partials 4..7, HIGH = partials 8..N) plus a
///     within-band `shape` taper into all 32 per-partial amplitudes.  This is
///     what slothrop's 2-knob Loom UI hooks into.
///
///   - **Low level** (`setPartialAmplitude`) overrides any single partial's
///     amplitude, for future modulation-matrix flexibility (e.g. per-partial
///     LFO).
///
/// `setBands` always writes all `kMaxPartials` amplitudes regardless of the
/// active `partialCount`, so band semantics are independent of CPU budget.
/// `partialCount` is purely a per-sample CPU knob: only the first
/// `partialCount` partials are summed by `process()`.
///
/// Output ceiling for ±1 amplitudes:
///   - `N` active partials at amplitude 1.0 sum in worst-case phase
///     alignment to ±N.  With `kMaxPartials = 32`, the strict upper bound
///     is ±32.  Random seeded phases mean the observed peak over any
///     practical window is much lower (CLT extreme ≈ √(N/2) × √(2 ln M)
///     ≈ ±18 for M = 1 s), but the documented ceiling is ±N.  The plugin
///     layer's PeakLimiter is responsible for the final headroom.
///
/// Real-time safe: all storage is `std::array`, no allocation in `process()`,
/// no exceptions, no locks.  Phase wrap (`phase -= floor(phase)`) is applied
/// every sample per CLAUDE.md "Phase wrapping" convention.
class HarmonicOscillator
{
public:
    /// Maximum partial count.  32 chosen as the Plaits Model 5 spectral
    /// density target — below this the upper band starves at common
    /// fundamentals (200–500 Hz), above this CPU cost outpaces audible
    /// return given Nyquist-muting of unsigned-int multiples beyond ~30.
    static constexpr int kMaxPartials = 32;

    /// Minimum fundamental frequency.  Below this the lowest partial sits
    /// in the sub-audio range and primitive is musically uninteresting; the
    /// floor also keeps `phaseInc = freq / sr` from becoming subnormal.
    static constexpr float kMinFreq = 10.0f;

    HarmonicOscillator();

    /// Initialise with sample rate, zero amplitudes, re-seed phase RNG.
    void prepare(float sampleRate);

    /// Re-seed phase RNG with the documented fixed constant.  Bit-exact
    /// reproducible across calls and sessions.  Does NOT clear amplitudes
    /// or partialCount (matches other ideath primitives — `reset()` is for
    /// internal state, not user-set parameters).
    void reset();

    /// Set the fundamental frequency (Hz).  Clamped to
    /// `[kMinFreq, sampleRate × 0.45]`.  All partial frequencies and
    /// Nyquist-alive flags are recomputed.
    void setFrequency(float hz);

    /// Set the active partial count.  Clamped to `[1, kMaxPartials]`.
    /// Only the first `n` partials are summed by `process()` — silenced
    /// partials still hold their amplitude (this is purely a CPU knob).
    void setPartialCount(int n);

    /// Set the amplitude of partial `idx` (0-indexed).  Clamped to `[0, 1]`.
    /// Out-of-range `idx` is silently ignored.
    void setPartialAmplitude(int idx, float amp);

    /// Plaits-style 3-band amplitude mapping with within-band taper.
    ///
    /// Each scalar is clamped to `[0, 1]`.  Bands are:
    ///   - LOW  = partials 1..3   (0-indexed 0..2,   width 3)
    ///   - MID  = partials 4..7   (0-indexed 3..6,   width 4)
    ///   - HIGH = partials 8..32  (0-indexed 7..31,  width 25)
    ///
    /// Within each band, partial `i` gets amplitude:
    ///   `band_amplitude × (1 - shape × band_pos)`
    /// where `band_pos = (i - band_start) / (band_width - 1)` ∈ `[0, 1]`.
    /// So `shape = 0` → flat band (all partials at full band weight),
    /// `shape = 1` → linear taper from band weight down to 0 at the band
    /// upper edge.
    ///
    /// Writes to all `kMaxPartials` amplitudes regardless of the active
    /// `partialCount` — band semantics are independent of CPU budget.
    void setBands(float low, float mid, float high, float shape);

    /// Produce one output sample.  Returns Σ over alive partials in
    /// `[0, partialCount)` of `amp[i] × sin(2π × phase[i])`.
    float process();

    // --- Introspection (test-only) ----------------------------------------
    float getPartialAmplitude(int idx) const;
    bool  isPartialAlive(int idx) const;

private:
    void recomputePartialFreqs();

    float sampleRate_   = 44100.0f;
    float frequency_    = 220.0f;
    int   partialCount_ = kMaxPartials;

    std::array<float, kMaxPartials> amplitude_{};   // per-partial, [0, 1]
    std::array<float, kMaxPartials> phase_{};       // [0, 1)
    std::array<float, kMaxPartials> phaseInc_{};    // freq / sr
    std::array<bool,  kMaxPartials> alive_{};       // false → above Nyquist guard

    /// Fixed seed for the phase RNG.  Documented so tests can assert
    /// determinism across reset() and prepare() calls.
    static constexpr std::uint32_t kPhaseSeed = 0xCAFEF00Du;
};

} // namespace ideath
