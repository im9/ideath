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
/// two different allpass decorrelator chains (equal total length, 18.75 ms)
/// make L and R.  Allpasses have unit magnitude, so both channels carry the
/// same energy at every frequency (a held note stays centred), while their
/// different dispersion decorrelates the diffuse tail.
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
    // Line-modulation LFOs as rotating unit phasors (re = cos, im = sin of
    // the phase): one complex multiply per sample instead of sin + cos,
    // renormalised every sample so the magnitude cannot drift.
    struct Phasor
    {
        float re = 1.0f;
        float im = 0.0f;
    };
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
    std::array<Diffuser, 4> decorrLeft_;
    std::array<Diffuser, 4> decorrRight_;
};

} // namespace ideath
