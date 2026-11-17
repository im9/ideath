#pragma once

#include <array>
#include <utility>
#include <vector>

namespace ideath {

/// Shared reverb tank behind HallReverb and ShimmerReverb.
///
/// 8-line feedback delay network (FDN) with Householder mixing, fed through
/// a 20 Hz second-order DC blocker and a 4-stage Schroeder input diffuser.
/// Every line carries a Jot absorptive gain (g_i = 10^(−3·L_i / (sr·T60))),
/// so the low-frequency T60 is exactly the one asked for regardless of line
/// length, and a 2nd-order Butterworth low-pass per line (DC gain 1) adds
/// high-frequency absorption on top.
/// All eight line reads are LFO-modulated through first-order allpass
/// interpolation (unit magnitude, so modulation costs no high end).
/// Stereo output: one tap reads all eight lines (a Hadamard sign row), then
/// two different allpass decorrelator chains (equal mean total length,
/// 18.75 ms) make L and R.  Allpasses have unit magnitude, so both channels
/// carry the same energy at every frequency (a held note stays centred),
/// while their different dispersion decorrelates the diffuse tail.  Each
/// decorrelator stage's delay is slowly modulated (±kDecorrModSec, its own
/// rate in kDecorrModRateMinHz … kDecorrModRateMaxHz), so the L/R phase
/// difference of a sustained partial keeps rotating: a held note's tail is
/// as wide as a noise tail on average at every pitch, instead of anywhere
/// from ρ = −0.94 to +1 depending on where the note falls.
///
/// Level: the input gain is set so that a stationary broadband input comes
/// out at the same RMS as it went in when T60 = kUnityT60.  The steady-state
/// RMS gain is ≈ √(T60 / kUnityT60) once T60 is long against the lines
/// (power gain ∝ T60, as in a real room); exactly b² / Σ(1 − g_i²), see
/// powerGain().
///
/// Mono in, stereo out.  Building block — HallReverb is this tank plus a
/// pre-delay; ShimmerReverb adds pitch-shifted voices.
class ReverbTank
{
public:
    static constexpr int kLines = 8;

    /// T60 at which the steady-state broadband RMS gain is 0 dB.  Equal to
    /// the HallReverb / ShimmerReverb T60 at size 0.5 (0.5 s · 60^0.5).
    static constexpr float kUnityT60 = 3.8729833f;

    /// setDecay clamp range (seconds).
    static constexpr float kMinT60 = 0.1f;
    static constexpr float kMaxT60 = 100.0f;

    /// High-frequency absorption at kDampRefHz when damp = 1, in dB per
    /// second on top of the 60 / T60 dB/s base decay.  Equals the former
    /// ShimmerReverb ceiling of 12 dB per ≈ 0.2 s loop pass.
    static constexpr float kMaxDampDbPerSec = 60.0f;
    static constexpr float kDampRefHz = 4000.0f;

    /// Peak line-read excursion at modDepth = 1 (seconds).
    static constexpr float kMaxModSec = 0.001f;

    /// Peak delay excursion of each output decorrelator stage (seconds).
    /// Fixed chains give a sustained partial ρ = cos Δφ(f); rotating Δφ
    /// averages that toward 0, but only if the rotation spans enough phase
    /// at the lowest notes: the phase a stage can add at f is ≈ 2π·f·A·k
    /// (k between 0.2 and 4.7, the Schroeder group-delay range at g = 0.65).
    /// Simulated time-averaged |ρ| for sustained sines 41–7040 Hz: 1 ms
    /// leaves 0.49 at 41–55 Hz, 2 ms brings the worst note to 0.16
    /// (measured in the tank: ≤ 0.23 for triangle tones C2–C6).  The
    /// shortest stage (113 samples at 48 kHz, 2.35 ms) keeps ≥ 0.35 ms;
    /// below ≈ 4.2 kHz sample rate the excursion is cut to keep every
    /// stage's delay ≥ 1.5 samples.
    static constexpr float kDecorrModSec = 0.002f;

    /// Decorrelator modulation rates (Hz): eight stages spread
    /// geometrically between these, alternating between the chains.  The
    /// fastest sets the pitch bend: a delay moving at 2π·f_m·A s/s shifts
    /// the pitch by that ratio — 1.9e−3 (3.3 cents at the stage's mean
    /// group delay) at 0.153 Hz.  The bend is held to no more than the
    /// tank's own line modulation bends a held note at its defaults; at
    /// 0.071–0.191 Hz the right chain measured 4.85 cents RMS at 4.4 kHz
    /// against the tank's 4.19, and the bend scales with the rate, so the
    /// range was scaled by 0.8.  The time-averaged ρ depends on the
    /// excursion, not the rate, so it is unchanged.  The slowest rate sets
    /// how long a held note takes to go through its whole decorrelation:
    /// one period, 17.5 s.
    static constexpr float kDecorrModRateMinHz = 0.057f;
    static constexpr float kDecorrModRateMaxHz = 0.153f;

    /// The decorrelator LFOs advance every this many samples; the delays
    /// ramp linearly in between.  A sine of excursion A sampled every K
    /// samples and joined by straight lines is off by at most
    /// A·(ω·K)²/8: 96 · (2π · 0.153 · 32 / 48000)² / 8 = 5e−6 samples at
    /// 48 kHz — nothing next to the 96-sample swing — and the pitch bend
    /// (the slope) is held for K / sr = 0.7 ms at a time, moving in steps
    /// of A·ω²·K / sr = 1.2e−6 s/s (0.002 cents).  Saves eight phasor
    /// rotations per sample.
    static constexpr int kDecorrControlSamples = 32;

    /// Freeze transition time (seconds).  Entering or leaving freeze moves
    /// the line gains, the damping mix-out and the input gain together,
    /// linearly over this time.  30 ms: a linear ramp of length T spreads
    /// each partial over ±1/T = ±33 Hz, inside the ≈ 100 Hz critical band
    /// at low frequencies, so it is heard as a level / timbre change and
    /// not as a click (20 ms would still fit at ±50 Hz; shorter ramps
    /// approach the one-sample step).  And it ends before the shortest line
    /// (41.6 ms) has recirculated once, so the hold is complete on the
    /// first pass, as with an instant switch.  The cost is the decay that
    /// still happens during the ramp — on average half the ramp of free
    /// decay, 60 dB · 0.015 s / T60: 1.8 dB at the shortest T60 (0.5 s),
    /// 0.2 dB at T60 3.9 s.  A 50 ms ramp would lose 3 dB at T60 0.5 s and
    /// run past the shortest line's first pass.
    static constexpr float kFreezeRampSec = 0.03f;

    /// First-order high-pass y = a·(y + x − x₁).  Zero gain at DC.  Its
    /// anti-denormal offset reaches the output as ε / (1 − a) only — a
    /// second-order (Biquad) high-pass at 20 Hz turns the same offset into
    /// ε / (1 + a₁ + a₂) ≈ 10⁵·ε of DC.
    struct DcBlocker
    {
        float a = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;
        void prepare(float cutoffHz, float sampleRate);
        void reset() { x1 = 0.0f; y1 = 0.0f; }
        float process(float x);
    };

    ReverbTank();

    void prepare(float sampleRate);
    void reset();

    /// Low-frequency T60 in seconds, clamped to [kMinT60, kMaxT60].
    void setDecay(float t60Seconds);

    /// Damping (0–1).  Extra absorption at 4 kHz rises linearly in dB/s from
    /// 0 to kMaxDampDbPerSec:  T60(4 kHz) = 60 / (60/T60 + 60·damp).
    void setDamp(float damp);

    /// Line modulation depth (0–1 → 0–1 ms peak excursion).
    void setModDepth(float depth);

    /// Line modulation rate in Hz, clamped to [0.05, 5].
    void setModRate(float rateHz);

    /// Freeze: line gains go to 1, damping off, input muted, so the tail is
    /// held indefinitely and nothing new enters it.  The change takes
    /// kFreezeRampSec in both directions (no click); from then on the
    /// frozen tank is exactly lossless and its input exactly zero.  The
    /// damping filters keep running on the line outputs while frozen, so
    /// leaving freeze picks them up with current state.
    void setFreeze(bool freeze);

    /// Process one sample entering the tank.  Returns wet {L, R}.
    std::pair<float, float> process(float input);

    /// The output tap (before the L/R decorrelators) from the last process()
    /// call.  Same power as L / R.
    float monoTap() const { return monoTap_; }

    /// LFO in [−1, 1] (quadrature of the line modulation), for consumers
    /// that want to move in step with the tank (shimmer voice detune).
    float lfo() const { return lfoOut_; }

    /// Low-frequency power gain from input to any one tap (L, R or mono):
    /// b² / Σ(1 − g_i²).  Equals the energy of the tap's impulse response
    /// per unit input energy (Parseval).  Always the unfrozen tank's gain
    /// (freeze does not change it).
    float powerGain() const { return powerGain_; }

    /// Power gain averaged over log frequency 25 Hz – 16 kHz (third-octave
    /// grid) — the gain a pink-spectrum input sees.  Below powerGain() when
    /// damping shortens the high-frequency decay.  Always the unfrozen
    /// tank's gain.
    float pinkPowerGain() const { return pinkPowerGain_; }

    float decaySeconds() const { return t60_; }
    float modDepth() const { return modDepth_; }
    /// Freeze target as last set (true from setFreeze(true) on, even while
    /// the ramp is still running — see freezeAmount()).
    bool frozen() const { return freeze_; }

    /// Freeze transition position: 0 = free, 1 = fully frozen, linear in
    /// between while the kFreezeRampSec ramp runs.
    float freezeAmount() const { return freezeAmount_; }

private:
    struct Line
    {
        std::vector<float> buffer;
        int mask = 0;
        int write = 0;
        float length = 0.0f;  // nominal delay (samples)
        float loss = 0.0f;    // 1 − g², g = Jot gain at the reference frequency
        float gain = 1.0f;    // Jot line gain (DC)
        // Damping low-pass: 2nd-order Butterworth (DC gain 1) in TPT
        // state-variable form, corner Ω_c = tan(π·f_c / sr).  Never
        // bypassed: with no damping the corner sits at kNoDampCorner·sr.
        float omegaC = 1.0f;
        float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f; // TPT coefficients from Ω_c
        float ic1 = 0.0f, ic2 = 0.0f;         // integrator states
        float apState = 0.0f; // allpass-interpolator output memory
    };

    struct Diffuser
    {
        std::vector<float> buffer;
        int size = 0;
        int index = 0;
        float g = 0.0f;
        float process(float x);
    };

    // LFOs (line and decorrelator modulation) as rotating unit phasors
    // (re = cos, im = sin of the phase): one complex multiply per sample
    // instead of sin + cos, renormalised every sample so the magnitude
    // cannot drift.
    struct Phasor
    {
        float re = 1.0f;
        float im = 0.0f;
    };

    // Output decorrelator stage: Schroeder allpass whose delay moves
    // slowly around `length`, read through the same first-order allpass
    // interpolator as the lines (unit magnitude, so the stage stays an
    // allpass at every delay).  `lfo` advances by `step` once per
    // kDecorrControlSamples; `delay` ramps toward it by `delayStep`.
    struct ModDiffuser
    {
        std::vector<float> buffer;
        int mask = 0;
        int write = 0;
        float length = 0.0f;    // nominal delay (samples)
        float excursion = 0.0f; // peak delay excursion (samples)
        float g = 0.0f;
        float apState = 0.0f;
        float delay = 0.0f;
        float delayStep = 0.0f;
        Phasor lfo;
        Phasor step;
        float process(float x);
    };

    void updateGains();
    float readLine(Line& line, float delay);

    float sampleRate_ = 44100.0f;
    float t60_ = kUnityT60;
    float damp_ = 0.3f;
    float modDepth_ = 0.5f;
    float modRate_ = 0.6f;
    bool freeze_ = false;
    float freezeAmount_ = 0.0f; // ramp position, 0 = free, 1 = frozen
    float freezeStep_ = 0.0f;   // ramp increment per sample

    float inputGain_ = 1.0f;
    float powerGain_ = 1.0f;
    float pinkPowerGain_ = 1.0f;
    float modSamples_ = 0.0f;
    Phasor lfo1_;
    Phasor lfo2_;
    Phasor lfoStep1_;
    Phasor lfoStep2_;
    float lfoOut_ = 0.0f;
    float monoTap_ = 0.0f;

    // Two cascaded 20 Hz blockers (12 dB/oct): keep infrasonic input out
    // of the DC mode.  The second also blocks the first's ε/(1 − a) offset.
    DcBlocker inputHp_;
    DcBlocker inputHp2_;
    std::array<Line, kLines> lines_;
    std::array<Diffuser, 4> diffusers_;
    int decorrCountdown_ = 0;
    std::array<ModDiffuser, 4> decorrLeft_;
    std::array<ModDiffuser, 4> decorrRight_;
};

} // namespace ideath
