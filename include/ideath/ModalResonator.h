#pragma once

#include <ideath/Biquad.h>
#include <ideath/Envelope.h>
#include <ideath/Noise.h>
#include <array>

namespace ideath {

/// Modal resonator / bell engine.
///
/// N parallel tuned band-pass filters (the "modes" or "partials"), each with
/// its own decay envelope.  A short noise burst fired by `strike()` excites
/// every mode at once; the sum is the output.  Increasing N adds more partials
/// (richer bell), increasing inharmonicity stretches the upper partials away
/// from the integer harmonic series (less bell-of-pure-tone, more
/// metallic-bell).
///
/// Per-partial structure (one of N, all sum to produce the output):
///
///     excitation  ──►  Biquad BP (freq = fundamental × ratio,
///                                 Q derived from per-partial decay)
///                              │
///                              ▼
///                       × Q (compensates BP's 0 dB peak normalisation
///                            so each partial's impulse-response peak is
///                            ≈ 1 / partialCount, summing to ≈ 1 in phase)
///                              │
///                              ▼
///                       × gain_[i] (per-partial amplitude shaping, default 1;
///                                    see setPartialGain)
///                              │
///                              ▼
///                         partial sum
///
/// The BP's intrinsic decay time T60 = Q × ln(1000) / (π × fc) is what
/// produces the audible ring; the per-partial decay setter sets Q to make
/// that T60 match the requested seconds.  There is no separate per-partial
/// amplitude envelope — the BP itself is the decay shape.
///
/// Excitation: when `strike(velocity)` is called we trigger an internal
/// DecayEnvelope (2 ms decay) and a Noise generator; their product is fed
/// into every active partial for the first ~10 ms after the strike.  After
/// the excitation envelope falls below its silence threshold the noise stops
/// and the modes ring on their own per-partial decays.  Velocity (clamped
/// `[0, 1]`) scales the excitation amplitude linearly.
///
/// Inharmonicity formula: at inharmonicity=0 partial ratios are exactly the
/// values set by `setPartialRatio` (default 1, 2, 3, …, n — the harmonic
/// series).  At inharmonicity=a in `(0, 1]` each effective ratio is stretched
/// by the piano-string formula
///
///     stretched(i) = ratio(i) × sqrt(1 + B × ratio(i)²)
///
/// with `B = a × 0.1`.  Stretching is monotonic in ratio so high partials
/// drift further than low ones, matching the spectrum of a real bell or
/// piano string.  Stretched ratios that would push a partial above
/// `sampleRate × 0.45` are silently muted (its mode is held in reset and
/// produces zero output) until the parameters move it back into range.
///
/// Output ceiling for ±1 input: each partial's BP output is multiplied by
/// its own Q at the sum stage and then by its per-partial gain (default 1,
/// clamped to `[0, 4]`), so each partial's peak amplitude is bounded by
/// ≈ excitation_amplitude × gain (≤ 4 from the noise burst × max gain).
/// N partials sum in worst-case phase alignment, so the paper worst-case
/// output is ≈ ±N × max(gain) = ±4N (= ±64 at kMaxPartials=16, up from ±16
/// at unity gain).  This is a paper worst case — all partials in phase at
/// max gain simultaneously — that typical output stays well below because
/// per-partial impulse responses do not peak simultaneously after the
/// stochastic 2 ms noise excitation, and Position-style weightings
/// redistribute rather than uniformly amplify.  The plugin layer's
/// PeakLimiter is responsible for the final ceiling.
///
/// Real-time safe: all storage is statically allocated
/// (`std::array<Biquad, kMaxPartials>` etc.); no `process()` path allocates.
class ModalResonator
{
public:
    /// Maximum number of partials.  16 is enough for any usable bell timbre
    /// — adding more above 16 is diminishing returns and runs into Nyquist
    /// muting for high fundamentals.
    static constexpr int kMaxPartials = 16;

    /// Minimum effective Q.  The actual per-partial Q is derived from the
    /// requested decay time and the partial frequency (see `setPartialDecay`
    /// for the formula); the floor prevents degenerate cases at very short
    /// decay × very low frequency from producing Q below 1 (where the BP
    /// stops behaving as a resonator).  Q=1 corresponds to a critically
    /// damped second-order section.
    static constexpr float kMinQ = 1.0f;

    /// Maximum effective Q.  Caps how sharp / long-ringing any partial can be.
    /// At fc=20 kHz × decay=30 s, the Q formula yields ≈ 27.3 k, which is
    /// numerically fine but musically meaningless and inflates the output
    /// scaling unnecessarily; the cap keeps Q in a useful range.
    static constexpr float kMaxQ = 5000.0f;

    ModalResonator();

    void prepare(float sampleRate);
    void reset();

    /// Set the fundamental frequency (Hz).  Clamped to
    /// `[10, sampleRate × 0.45]`.  All partial frequencies are recomputed.
    void setFundamental(float hz);

    /// Set the number of active partials.  Clamped to `[1, kMaxPartials]`.
    /// Inactive partials are held in reset and contribute zero output.
    void setPartialCount(int n);

    /// Set the harmonic ratio of partial `idx` to the fundamental.  The
    /// partial's frequency becomes `fundamental × ratio` (before inharmonicity
    /// stretching).  Index out-of-range is silently ignored.  Ratio is
    /// clamped to `[0.01, 64.0]` — 0.01 because the formula divides by
    /// nothing useful below that, 64 because higher ratios are always
    /// past Nyquist for any musical fundamental.
    void setPartialRatio(int idx, float ratio);

    /// Set the decay time (T60, seconds) of partial `idx`.  Clamped to
    /// `[0.001, 30.0]` — 1 ms minimum (a single block of partial ring,
    /// reasonable for very tight metallic clicks), 30 s maximum (sustains
    /// well beyond any musical bell).  Index out-of-range is silently
    /// ignored.
    ///
    /// Mapped to BP Q via `Q = π × fc × decay / ln(1000)`, the closed-form
    /// derivation of the 2nd-order resonator pole magnitude that yields a
    /// −60 dB impulse-response envelope in exactly `decay` seconds at centre
    /// frequency `fc`.  Q is then clamped to `[kMinQ, kMaxQ]`.
    void setPartialDecay(int idx, float seconds);

    /// Inharmonicity amount, clamped to `[0, 1]`.  See class comment for
    /// the stretching formula.  Recomputes all partial coefficients.
    void setInharmonicity(float amount);

    /// Set the amplitude gain of partial `idx` at the sum stage.  Multiplies
    /// the partial's Q-compensated output before summation.  Clamped to
    /// [0, 4].  Default 1.0 for all partials.  Index out-of-range is
    /// silently ignored.
    ///
    /// Intended for modelling strike-position dependence of modal
    /// amplitudes (Mutable Instruments Rings-style Position knob): shaping
    /// which modes ring loudest without changing their pitch or decay.
    /// Setting gain=0 mutes the partial audibly but leaves its BP running
    /// (state is preserved for reintroduction) — unlike alive_ Nyquist-mute
    /// which holds the BP in reset.
    ///
    /// The [0, 4] ceiling gives 4× headroom over unity so a Rings-style
    /// weighting curve that concentrates energy into a subset of partials
    /// (e.g. sin(π · i · pos) with pos-shifted maxima) can preserve total
    /// loudness after the redistribution.  Higher gains are still bounded
    /// by the PeakLimiter downstream; the cap here just prevents an
    /// accidental 1000× that would nuke the sum.
    void setPartialGain(int idx, float gain);

    /// Strike the resonator: fires the noise-burst excitation that excites
    /// all active modes.  `velocity` is clamped to `[0, 1]` and scales the
    /// excitation amplitude linearly.
    void strike(float velocity);

    /// Produce one output sample.  Returns the sum of all active partials'
    /// outputs after envelope scaling.
    float process();

private:
    void updatePartialCoefficients();
    void updatePartial(int i);

    float sampleRate_   = 44100.0f;
    float fundamental_  = 220.0f;
    float inharmonicity_ = 0.0f;
    int   partialCount_ = 8;

    std::array<Biquad,         kMaxPartials> partial_{};
    std::array<float,          kMaxPartials> ratio_{};   // user-set
    std::array<float,          kMaxPartials> decay_{};   // user-set, seconds
    std::array<float,          kMaxPartials> qCached_{}; // derived from decay × freq
    std::array<bool,           kMaxPartials> alive_{};   // false → muted (Nyquist)
    std::array<float,          kMaxPartials> gain_{};    // per-partial output gain, default 1

    // Excitation: noise × decay envelope (a short burst at strike time).
    Noise          excNoise_{};
    DecayEnvelope  excEnv_{};
    static constexpr float kExcitationDecay = 0.002f; // 2 ms — short noise burst
};

} // namespace ideath
