#pragma once

#include <vector>

namespace ideath {

/// Bowed-string physical model — friction-driven sibling of `KarplusStrong`.
///
/// Two-segment digital waveguide (Smith's bowed string; the same layout as
/// STK's `Bowed`).  The bow sits at fraction β = position of the string
/// length from the bridge and splits the string into
///
///   bridge segment: round trip β·L samples, ends in the bridge reflection
///                   −g·LP(z) (inverting, lossy, low-passed)
///   nut segment:    round trip (1 − β)·L samples, ends in the nut
///                   reflection −1 (inverting, lossless)
///
/// where L = one period minus the bridge low-pass's phase delay at the
/// fundamental, so the whole loop — both segments plus the filter — is
/// exactly one period long (the two inversions cancel).  This is what makes
/// Helmholtz motion possible: a single corner circulates the string, the
/// bow sticks for (1 − β) of the period and slips for β of it, and the
/// motion repeats once per loop.  A single delay loop with the friction
/// map applied once per period (the previous model) has no such structure;
/// it period-doubles to f0/2 as soon as the map's slope passes −1.
///
/// Bow–string junction.  The two waves arriving at the bow point sum to
/// the string velocity the bow would see if it were not there,
/// v_h = v_from_nut + v_from_bridge.  With Δ = v_bow − v_h the bow adds the
/// same velocity Δ_out = φ(Δ) to both outgoing waves:
///
///   φ(Δ) = Δ · min(1, (|Δ| / w + 0.75)^−4),   w = pressure · kStickWidth · |v_bow|
///
/// (Smith's bow table; the STK curve).  For |Δ| ≤ w/4 the reflection is 1:
/// the string is carried at exactly the bow velocity (stick).  Beyond it
/// the coupling falls off steeply (slip).  w — how far the string may be
/// pulled away from the bow speed before it breaks free — scales with the
/// bow speed, so `pressure` is bow force relative to bow speed: the motion
/// is the same at every speed, only its amplitude scales with |v_bow|.
/// Bow speed 0 means no coupling at all (the string rings freely).
///
/// Minimum pressure (Schelleng).  Below a pressure threshold the bow just
/// slides: the string settles into a static deflection and any attack
/// transient dies away at the string's own decay rate.  Above it the
/// sliding state is unstable and Helmholtz motion builds up.  The threshold
/// follows from linearising φ about the sliding state and finding where a
/// root of the loop's characteristic equation leaves the unit circle;
/// `minimumPressure()` computes it.  It depends on pitch, position and
/// damping, not on the bow speed (scale invariance above).
///
/// Output is the velocity wave arriving at the bridge — proportional to
/// the transverse force on the bridge, the signal a real instrument body
/// radiates — times `kOutputGain`.  It is mono and scales with |v_bow|.
///
/// Real-time safe: the two delay buffers allocate only inside `prepare()`;
/// `process()` allocates nothing, throws nothing, takes no locks.  Every
/// buffer write and the low-pass state carry the library's +1e-25 DC
/// offset against denormals; the bow-speed smoother flushes to its target
/// once within 1e-9.
class BowedString
{
public:
    BowedString() = default;

    /// Allocate the delay buffers (sized for the lowest supported pitch).
    void prepare(float sampleRate);

    /// Zero both delay lines, the low-pass memory and the bow-speed
    /// smoother.  Bit-exact reproducibility under identical parameter
    /// streams.
    void reset();

    /// Set the pitch (Hz).  Clamped to `[kMinFreq, sampleRate × 0.45]`.
    void setFrequency(float hz);

    /// Bow velocity, clamped to `[-1, 1]`.  The sign is the bow direction
    /// (timbre is the same either way); the magnitude scales the output.
    /// The model follows changes through a one-pole smoother with time
    /// constant `kBowSmoothingSec`, so a bow placed on the string at speed
    /// does not start with a step (no click, no attack kick).
    void setBowVelocity(float v);

    /// Bow pressure relative to bow speed, clamped to `[0, 1]`.  Below
    /// `minimumPressure()` the bow slides without sustaining the string.
    void setPressure(float p);

    /// Bow position as a fraction of the string length measured from the
    /// bridge.  Clamped to `[kMinPosition, kMaxPosition]`.  Near the bridge
    /// (small values) the tone is bright and needs more pressure to speak;
    /// toward the middle it is softer.
    void setPosition(float p);

    /// Damping in `[0, 1]`.  Sets the decay after the bow lets go, from
    /// `kMaxDecaySec` (0) to `kMinDecaySec` (1), and the bridge low-pass
    /// `y = (1 − c)·x + c·y[n−1]` with c = kMaxBridgeLowpass × damping
    /// (more damping → darker).  The loop gain compensates the low-pass's
    /// and the interpolator's loss at the fundamental so the decay time is
    /// what damping asks for, up to the kMaxLoopGain ceiling (it binds at
    /// high pitch with long decays, above ≈ 1.4 kHz at damping 0).
    void setDamping(float d);

    /// Produce one output sample.
    float process();

    /// Schelleng minimum pressure for the current pitch, position and
    /// damping (see the class comment).  Below it the string does not
    /// speak; above it, it does.  Returns +infinity when no pressure
    /// in the model can destabilise the sliding state (only above ≈ 12 kHz
    /// at 44.1 kHz; up to there the threshold stays in 0.017-0.43).
    /// Independent of the bow speed.  Costs ≈ 25-60 µs (M2 Max) — call it
    /// when a parameter changes, not per sample or per block.
    float minimumPressure() const;

    /// The same threshold for explicit parameters (values are clamped as
    /// the setters clamp them).
    static float minimumPressure(float sampleRate, float frequency,
                                 float position, float damping);

    // ---- spec constants --------------------------------------------------

    static constexpr float kMinFreq = 30.0f;

    /// Closest bow position to the bridge, as a fraction of the string.
    static constexpr float kMinPosition = 0.02f;
    /// The bridge segment is never shorter than this many samples (round
    /// trip), except at the very top of the pitch range (above ≈ 14.5 kHz,
    /// where the loop is under 3 samples and the nut segment keeps 1).  With a one-sample bridge segment the bow position is
    /// quantised to the sample grid and the motion beats (partials 1-3
    /// spread over ±25 cents at C6, β = 0.02); two samples keep it periodic.
    /// Binds once β·L rounds below 2, i.e. above f = β · sr / 1.5 (≈ 590 Hz
    /// at 44.1 kHz for the bridge-most β = 0.02): there the bow sits a
    /// 2-sample round trip from the bridge.
    static constexpr int kMinBridgeSamples = 2;
    /// Furthest bow position from the bridge.  At exactly mid-string the
    /// two segments are equal, every even mode has a node under the bow and
    /// the motion alternates between two different periods (≈ 10 % of the
    /// energy lands on (n + ½)·f0).  0.45 keeps the segments distinct.
    static constexpr float kMaxPosition = 0.45f;

    /// Stick width per unit pressure and bow speed: w = p · kStickWidth · |v|.
    /// The string sticks while it is within w/4 of the bow speed, so at
    /// p = 1 the bow can hold the string against a velocity difference of
    /// 1.5 × the bow speed, and the Schelleng threshold lands between
    /// p ≈ 0.02 and p ≈ 0.15 over the clamped position / damping ranges
    /// from 30 Hz to C6 (up to 0.43 at the top of the pitch range).
    static constexpr float kStickWidth = 6.0f;

    /// Bridge low-pass coefficient at damping = 1 (−3 dB at ≈ 8.2 kHz at
    /// 44.1 kHz).  The low-pass rounds the Helmholtz corner, and a rounded
    /// corner trips the slip early, so the period shortens as the pressure
    /// rises; at 0.5 that reaches +13 cents at C6, at 0.35 it stays inside
    /// ±10 cents over the whole pressure range.
    static constexpr float kMaxBridgeLowpass = 0.35f;

    /// Hard ceiling on the bridge reflection gain (always < 1).
    static constexpr float kMaxLoopGain = 0.9995f;

    /// Decay time (−60 dB) after the bow lets go, at damping 0 and 1.
    static constexpr float kMaxDecaySec = 10.0f;
    /// 1 s rather than shorter: with a loop that loses more than −60 dB
    /// per second the bow only just sustains it, and just above the
    /// threshold the string falls into a near-sinusoidal regime whose
    /// upper partials are 40 dB down and wander in pitch.
    static constexpr float kMinDecaySec = 1.0f;

    /// Bow-speed smoothing time constant.  5 ms is short against any bow
    /// gesture and long against one period of every supported pitch.
    static constexpr float kBowSmoothingSec = 0.005f;

    /// Output scale applied to the bridge wave.
    static constexpr float kOutputGain = 0.25f;

private:
    struct Loop
    {
        int   bridgeDelay = 1;     // integer samples
        int   nutDelayInt = 1;     // integer part of the nut-segment delay
        float nutDelayFrac = 0.0f; // linear-interpolation fraction
        float lowpass = 0.0f;      // bridge low-pass coefficient c
        float loopGain = 0.99f;    // bridge reflection gain g
    };

    static Loop computeLoop(float sampleRate, float frequency,
                            float position, float damping);
    void recomputeLoop();

    float sampleRate_   = 44100.0f;
    float frequency_    = 220.0f;
    float bowVelocity_  = 0.0f;
    float bowSmoothed_  = 0.0f;
    float bowCoef_      = 0.0f;
    float pressure_     = 0.5f;
    float position_     = 0.1f;
    float damping_      = 0.2f;

    Loop loop_;

    std::vector<float> bridgeBuf_;
    std::vector<float> nutBuf_;
    int   mask_       = 0;
    int   writePos_   = 0;
    float lpState_    = 0.0f;
};

} // namespace ideath
