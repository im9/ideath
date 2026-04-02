#pragma once

#include "Envelope.h"
#include <cmath>

namespace ideath {

/// 4-operator FM synthesizer (YM2612-inspired).
/// 8 algorithms define how operators modulate each other.
/// Each operator has its own ADSR envelope, ratio, level, and feedback.
class FMSynth
{
public:
    static constexpr int kNumOperators = 4;
    static constexpr int kNumAlgorithms = 8;

    FMSynth() = default;

    void prepare(float sampleRate);
    void reset();

    // --- Note control ---
    void noteOn(float freqHz, float velocity = 1.0f);
    void noteOff();
    bool isActive() const;

    // --- Algorithm ---
    /// Set routing algorithm (0-7).
    void setAlgorithm(int algo);
    int getAlgorithm() const { return algorithm_; }

    // --- Per-operator parameters ---
    /// Frequency ratio (0.5-16.0). Multiplied with base frequency.
    void setRatio(int op, float ratio);
    /// Output level (0.0-1.0).
    void setLevel(int op, float level);
    /// Self-modulation feedback (0.0-1.0).
    void setFeedback(int op, float amount);

    // --- Per-operator envelope ---
    void setAttack(int op, float seconds);
    void setDecay(int op, float seconds);
    void setSustain(int op, float level);
    void setRelease(int op, float seconds);

    /// Process one sample.
    float process();

private:
    struct Operator
    {
        float phase = 0.0f;
        float phaseInc = 0.0f;
        float ratio = 1.0f;
        float level = 1.0f;
        float feedback = 0.0f;
        float prevOut = 0.0f;
        AdsrEnvelope env;

        void prepare(float sampleRate) { env.prepare(sampleRate); }
        void reset()
        {
            phase = 0.0f;
            prevOut = 0.0f;
            env.reset();
        }

        /// Tick operator: modIn is phase modulation input.
        float tick(float modIn)
        {
            float fb = prevOut * feedback * 3.2f;
            float out = std::sin(phase * 6.283185307f + modIn + fb) * env.process() * level;
            phase += phaseInc;
            phase -= std::floor(phase);
            prevOut = out;
            return out;
        }
    };

    float sampleRate_ = 44100.0f;
    float baseFreq_ = 440.0f;
    float velocity_ = 1.0f;
    int algorithm_ = 0;

    Operator ops_[kNumOperators];

    void updatePhaseIncrements();
};

} // namespace ideath
