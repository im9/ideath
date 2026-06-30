#pragma once

#include "Envelope.h"
#include "LFO.h"
#include <cstdint>

namespace ideath {

/// 6-operator DX7-style FM synthesizer.
///
/// 32 algorithms with DX7 wiring (data sourced from dexed/msfa, the canonical
/// open-source DX7 emulator).  Per-operator: ratio (continuous, NOT DX7's
/// quantized coarse+fine), fine detune in cents, level, ADSR envelope,
/// feedback, velocity sensitivity, LFO PM/AM sensitivity.  One global LFO.
///
/// This is the general-purpose / DX-leaning FM primitive.  For chiptune-style
/// FM (YM2612 / Sega Mega Drive — 4 op × 8 algo, raw aliasing intact),
/// use `FMSynth` instead.  This primitive is NOT DX7-patch-compatible (no
/// rate scaling, no key scaling level, no envelope rate-vs-level model);
/// it follows DX7's algorithm topology but uses ADSR envelopes for
/// simplicity and exposes parameters as raw physical units.
///
/// Operator indexing convention: `op[0]` corresponds to DX7's OP6 (the
/// feedback-capable modulator in most algorithms), `op[5]` to DX7's OP1
/// (typically a carrier).  This matches dexed/msfa storage order so the
/// algorithm table data can be reused verbatim.
class DXFMSynth
{
public:
    static constexpr int kNumOperators = 6;
    static constexpr int kNumAlgorithms = 32;

    enum class LFOShape : int { Sine, Triangle, Square, Saw, SampleHold };

    DXFMSynth() = default;

    void prepare(float sampleRate);
    void reset();

    // --- Note control ---
    void noteOn(float freqHz, float velocity = 1.0f);
    void noteOff();
    bool isActive() const;

    // --- Algorithm ---
    /// 0-based algorithm index (0..31 → DX7 algorithms 1..32).
    void setAlgorithm(int algo);
    int getAlgorithm() const { return algorithm_; }

    /// True if operator `op` (0..5) outputs to the main bus under the given
    /// algorithm (0..31) — i.e. it's a carrier.  Returns false out of range.
    static bool isCarrier(int algorithm, int op);

    // --- Per-operator parameters (op index 0..5) ---
    /// Frequency ratio (0.5 - 32.0) applied to base note frequency.
    void setRatio(int op, float ratio);
    /// Fine detune in cents (±100).  Multiplicative on top of `setRatio`.
    void setDetune(int op, float cents);
    /// Static output level (0.0 - 1.0); multiplied by envelope per sample.
    void setLevel(int op, float level);
    /// Self-feedback amount (0.0 - 1.0).  Only operators marked as feedback
    /// in the current algorithm respond — others ignore this setting.
    void setFeedback(int op, float amount);

    void setAttack(int op, float seconds);
    void setDecay(int op, float seconds);
    void setSustain(int op, float level);
    void setRelease(int op, float seconds);

    /// Velocity sensitivity (0.0 = ignore velocity, 1.0 = full velocity tracking).
    void setVelocitySensitivity(int op, float sens);
    /// LFO pitch modulation sensitivity per op (0.0 - 1.0).  At 1.0 with LFO
    /// depth=1.0, the LFO swings the op's frequency by ±100 cents.
    void setPMSensitivity(int op, float sens);
    /// LFO amplitude modulation sensitivity per op (0.0 - 1.0).  At 1.0 with
    /// LFO depth=1.0, the LFO modulates op amplitude over [0, 2× nominal].
    void setAMSensitivity(int op, float sens);

    // --- Global LFO ---
    void setLFOShape(LFOShape s);
    void setLFORate(float hz);
    /// Overall LFO depth (0.0 - 1.0); per-op PMS/AMS scale this further.
    void setLFODepth(float depth);

    /// Process one sample.
    float process();

private:
    struct Operator
    {
        float phase = 0.0f;
        float phaseInc = 0.0f;
        float ratio = 1.0f;
        float detuneCents = 0.0f;
        float level = 0.0f;
        float feedback = 0.0f;
        float velSens = 1.0f;
        float pmSens = 0.0f;
        float amSens = 0.0f;
        float prevOut = 0.0f;
        AdsrEnvelope env;

        void prepareAndReset(float sr)
        {
            env.prepare(sr);
            phase = 0.0f;
            prevOut = 0.0f;
        }
    };

    float sampleRate_ = 44100.0f;
    float baseFreq_ = 440.0f;
    float velocity_ = 1.0f;
    int algorithm_ = kNumAlgorithms - 1;  // DX7 alg 32 (additive) as default
    int carrierCount_ = 1;                // recomputed on setAlgorithm

    Operator ops_[kNumOperators];
    LFO lfo_;
    float lfoDepth_ = 0.0f;

    // 2-sample-averaged feedback (suppresses 1-sample-delay aliasing in self-
    // modulated operators — same trick used by dexed/msfa).
    float feedbackTail_ = 0.0f;
    float feedbackPrev_ = 0.0f;

    void updatePhaseIncrements();
};

} // namespace ideath
