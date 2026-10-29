#pragma once

#include "Biquad.h"
#include "ReverbTank.h"
#include <array>
#include <utility>
#include <vector>

namespace ideath {

/// Shimmer reverb (Strymon BigSky Shimmer lineage): pre-delay → ReverbTank
/// (the same 8-line FDN as HallReverb) plus two pitch-shift voices.
///
/// Voices: each has its own interval (continuous semitones, −24…+24) and
/// level.  Upward voices sit behind a 4th-order anti-alias low-pass; downward
/// voices are followed by a 120 Hz high-pass to keep the low end clean.
/// The shifter is a 4-grain overlap-add of 100 ms Hann grains at 75 %
/// overlap.  Each new grain starts at the delay (within a 12 ms span) that
/// best correlates with what the other grains are playing, so all four add
/// in phase: the voice is a clean tone at f·r with no grain-rate buzz.  Each
/// voice is scaled by how coherently its grains add (measured from the same
/// grain reads), so it is unity-power whether its input is coherent (a
/// drone) or diffuse, short notes included.  The voices' source is
/// high-passed at 20 Hz (pitch shifting maps DC to DC, where the tank's
/// gain is highest).
///
/// Modes:
///  - Feedback: the voices read the tank and feed back into it, so each
///    pass climbs another interval (the classic rising shimmer).
///  - Input: the voices shift the input before it enters the tank — no
///    regeneration, the interval never stacks.
///
/// Shimmer amount is defined as an energy ratio, independent of decay:
/// each generation of shifted sound carries ρ = amount² · ρ_max · P_v of
/// the energy of the sound it came from (ρ_max = 0.5 in Feedback, 1.0 in
/// Input; P_v = min(1, Σ level²)).  The tank's unshifted decay never
/// changes with the amount.  The wet output is scaled by 1 / √(Σ energy
/// of all generations) — 1/√(1 + ρκ) in Input mode, √(1 − ρκλ) in
/// Feedback mode, with κ = the share of a pink source the voices' filters
/// pass and λ = the tank's pink-weighted / low-frequency power gain (the
/// share of each generation's energy the damping lets back) — so the level
/// matches HallReverb at any amount (pink noise, ±1 dB).
///
/// Freeze: the tank becomes lossless, input is muted and no new shifted
/// sound is injected, so the held tail keeps its pitch content and never
/// climbs.
///
/// Mono in, stereo out.  Output ceiling for ±1 input: ±kMaxOutput (35),
/// same derivation as HallReverb (the wet output is RMS-normalised to the
/// Hall level; shifted generations land on other frequencies, so their mode
/// peaks add in power, not in amplitude).  Place a PeakLimiter downstream.
class ShimmerReverb
{
public:
    static constexpr int kNumVoices = 2;
    static constexpr float kMaxOutput = 35.0f;
    static constexpr float kMaxPreDelay = 0.12f; // seconds
    static constexpr float kMaxSemitones = 24.0f;

    enum class Mode { Feedback, Input };

    ShimmerReverb();

    void prepare(float sampleRate);
    void reset();

    /// Reverb size (0.0–1.0).  Low-frequency T60 = 0.5 s · 60^size, the
    /// same map as HallReverb.  Independent of the shimmer amount.
    void setSize(float size);

    /// Damping (0.0–1.0).  High-frequency absorption: extra decay at 4 kHz
    /// rises linearly from 0 to 60 dB/s.
    void setDamp(float damp);

    /// Shimmer amount (0.0–1.0).  0 = plain reverb (no shifted sound at
    /// all); 1 = each generation carries ρ_max of its source's energy.
    void setShimmer(float amount);

    /// Dry/wet mix (0.0 = fully dry, 1.0 = fully wet).
    void setMix(float mix);

    /// Freeze mode.  Infinite sustain when true.
    void setFreeze(bool freeze);

    /// Pre-delay in seconds (0.0–0.12).
    void setPreDelay(float seconds);

    /// Tank modulation depth (0.0–1.0 → 0–1 ms line excursion).  Also
    /// detunes the voices by up to ±kMaxDetuneCents (chorus-like).
    void setModDepth(float depth);

    /// Tank modulation rate in Hz (0.05–5).
    void setModRate(float rateHz);

    /// Voice interval in semitones, clamped to [−24, +24].  Voice 0
    /// defaults to +12, voice 1 to +19.
    void setVoicePitch(int voice, float semitones);

    /// Voice level (0.0–1.0).  Voice 0 defaults to 1, voice 1 to 0.
    void setVoiceLevel(int voice, float level);

    /// Feedback (regenerating) or Input (single shift).  Default Feedback.
    void setMode(Mode mode);

    /// Process one mono input sample.  Returns {left, right}.
    std::pair<float, float> process(float input);

    static constexpr float kMaxDetuneCents = 8.0f;

private:
    struct Voice
    {
        static constexpr int kGrains = 4;

        std::vector<float> buffer;
        int mask = 0;
        int write = 0;
        float semitones = 0.0f;
        float ratio = 1.0f;
        float level = 0.0f;
        std::array<float, kGrains> phase{};
        std::array<float, kGrains> delay{};
        Biquad aa1, aa2; // anti-alias (upward voices)
        Biquad hp;       // low cut (downward voices)
        float sumPower = 0.0f;  // smoothed power of the grain sum
        float readPower = 0.0f; // smoothed power the grains read
        std::vector<float> reference;      // splice search scratch (corr window)
        float pinkGain = 1.0f;             // pink energy kept by the filters
    };

    void updateShimmer();
    void setVoiceRatio(Voice& v, float semitones);
    float processVoice(Voice& v, float input, float detuneCents);
    int bestSplice(Voice& v, int grain, int lo, int hi) const;
    float voicePinkGain(float ratio) const;

    float sampleRate_ = 44100.0f;
    float size_ = 0.7f;
    float damp_ = 0.3f;
    float shimmer_ = 0.5f;
    float mix_ = 0.5f;
    bool freeze_ = false;
    Mode mode_ = Mode::Feedback;

    // Derived
    float rho_ = 0.0f;        // energy ratio per generation
    float voiceNorm_ = 1.0f;  // 1 / max(1, √Σ level²)
    float outNorm_ = 1.0f;    // 1 / √(1 + Σ generations)
    float lambda_ = 1.0f;     // G_pink / G (Feedback level normalisation)
    float inputShiftGain_ = 0.0f;

    float grainSamples_ = 0.0f;
    int searchSamples_ = 0;
    int corrSamples_ = 0;
    std::vector<float> window_; // Hann, kWindowSize + 1 points
    static constexpr int kWindowSize = 1024;
    ReverbTank::DcBlocker sourceHp_;
    float grainPowerCoeff_ = 0.0f;

    std::vector<float> preDelay_;
    int preDelayMask_ = 0;
    int preDelayWrite_ = 0;
    int preDelaySamples_ = 0;
    float preDelaySec_ = 0.0f;

    std::array<Voice, kNumVoices> voices_;

    ReverbTank tank_;
};

} // namespace ideath
