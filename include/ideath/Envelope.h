#pragma once

namespace ideath {

/// Simple decay envelope (trigger → exponential decay → off).
/// Good for drums and percussive sounds.
class DecayEnvelope
{
public:
    DecayEnvelope() = default;

    void prepare(float sampleRate);
    void reset();

    /// Set decay time in seconds.
    void setDecay(float seconds);

    void trigger(float level = 1.0f);

    /// Returns next envelope value.
    float process();

    bool isActive() const { return active_; }
    float getValue() const { return level_; }

private:
    float sampleRate_ = 44100.0f;
    float level_ = 0.0f;
    float decayCoef_ = 0.9995f;
    bool active_ = false;
    static constexpr float kSilenceThreshold = 1e-5f;
};

/// ADSR envelope for sustained sounds.
class AdsrEnvelope
{
public:
    enum class Stage { Idle, Retrigger, Attack, Decay, Sustain, Release };

    AdsrEnvelope() = default;

    void prepare(float sampleRate);
    void reset();

    void setAttack(float seconds);
    void setDecay(float seconds);
    void setSustain(float level);   // 0.0–1.0
    void setRelease(float seconds);

    void noteOn();
    void noteOff();

    float process();

    bool isActive() const { return stage_ != Stage::Idle; }
    Stage getStage() const { return stage_; }
    float getValue() const { return level_; }

private:
    float sampleRate_ = 44100.0f;
    float level_ = 0.0f;
    float attackRate_ = 0.0f;
    float decayCoef_ = 0.0f;
    float sustainLevel_ = 0.5f;
    float releaseCoef_ = 0.0f;
    float retriggerCoef_ = 0.0f;
    Stage stage_ = Stage::Idle;

    static float calcCoef(float timeSeconds, float sampleRate);
};

/// Attack/Release envelope for slow fade in/out (per-layer swells, gates).
/// Sustains at 1.0 between noteOn and noteOff.
class AREnvelope
{
public:
    enum class Stage { Idle, Attack, Sustain, Release };

    AREnvelope() = default;

    void prepare(float sampleRate);
    void reset();

    void setAttack(float seconds);
    void setRelease(float seconds);

    void noteOn();
    void noteOff();

    float process();

    bool isActive() const { return stage_ != Stage::Idle; }
    Stage getStage() const { return stage_; }
    float getValue() const { return level_; }

private:
    float sampleRate_ = 44100.0f;
    float level_ = 0.0f;
    float attackRate_ = 0.0f;
    float releaseCoef_ = 0.0f;
    Stage stage_ = Stage::Idle;
};

} // namespace ideath
