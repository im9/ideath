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

    /// Freeze: line gains go to 1, damping off.  The caller decides what to
    /// feed (HallReverb / ShimmerReverb feed silence while frozen).
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
    /// per unit input energy (Parseval).  Infinite while frozen.
    float powerGain() const { return powerGain_; }

    /// Power gain averaged over log frequency 25 Hz – 16 kHz (third-octave
    /// grid) — the gain a pink-spectrum input sees.  Below powerGain() when
    /// damping shortens the high-frequency decay.  Infinite while frozen.
    float pinkPowerGain() const { return pinkPowerGain_; }

    float decaySeconds() const { return t60_; }
    float modDepth() const { return modDepth_; }
    bool frozen() const { return freeze_; }

private:
    struct Line
    {
        std::vector<float> buffer;
        int mask = 0;
        int write = 0;
        float length = 0.0f;  // nominal delay (samples)
        float loss = 0.0f;    // 1 − g², g = Jot gain at the reference frequency
        float gain = 1.0f;    // Jot line gain (DC)
        // Damping low-pass (2nd-order Butterworth, DC gain 1); omegaC = 0
        // means bypassed.
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
        float omegaC = 0.0f;
        void setBypass()
        {
            b0 = 1.0f;
            b1 = b2 = a1 = a2 = 0.0f;
            omegaC = 0.0f;
        }
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

    float inputGain_ = 1.0f;
    float powerGain_ = 1.0f;
    float pinkPowerGain_ = 1.0f;
    float modSamples_ = 0.0f;
    float lfoPhase_ = 0.0f;
    float lfoPhase2_ = 0.0f;
    float lfoInc_ = 0.0f;
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
