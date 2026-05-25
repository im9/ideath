#pragma once

namespace ideath {

/// West Coast style function generator (Make Noise 0-Coast Contour,
/// Make Noise MATHS rise/fall + cycle + EOC). Rise → Fall envelope with
/// shared curve shaping; one-shot or self-cycling. Distinct from
/// AdsrEnvelope: this one has no sustain stage and exposes an
/// end-of-cycle pulse for inter-module routing.
class FunctionGenerator
{
public:
    enum class Stage { Idle, Rise, Fall };

    FunctionGenerator() = default;

    void prepare(float sampleRate);
    void reset();

    /// Rise segment time in seconds.  Clamped to [0.001, 4.0].
    void setRise(float seconds);

    /// Fall segment time in seconds.  Clamped to [0.001, 8.0].
    void setFall(float seconds);

    /// Curve shape, shared by rise and fall.
    ///   curve = 0   : linear
    ///   curve > 0   : t^N — slow start, fast end (exponential swell)
    ///   curve < 0   : 1 - (1-t)^N — fast start, slow end (log-ish pluck)
    /// Clamped to [-1, +1].  N = 1 + |curve| * k with k = 2.
    void setCurve(float curve);

    /// When true the generator loops continuously (LFO mode).  Setting it
    /// true from Idle auto-starts the first cycle without needing trigger().
    /// Setting it false mid-cycle lets the current rise+fall complete
    /// naturally before going idle (no mid-segment drop to zero).
    void setCycle(bool on);

    /// Trigger a rise→fall cycle.
    ///   From Idle: starts a fresh rise from 0.
    ///   From Rise: no-op (continues current rise).
    ///   From Fall: switches to Rise, starting from the current output
    ///              value (continuous, no click, no reversal).
    void trigger();

    /// Process one sample, returns the current envelope value in [0, 1].
    float process();

    /// Returns true exactly once per fall completion.  Reading clears the
    /// pulse.  Safe to poll at block boundaries (a cycle is always many
    /// blocks long for any realistic rise/fall setting).
    bool consumeEoc();

    bool isActive() const { return stage_ != Stage::Idle || cycle_; }
    float currentValue() const { return currentValue_; }
    Stage getStage() const { return stage_; }

private:
    float sampleRate_ = 44100.0f;
    float riseTime_ = 0.01f;
    float fallTime_ = 0.1f;
    float phaseIncRise_ = 0.0f;
    float phaseIncFall_ = 0.0f;
    float phase_ = 0.0f;            // [0, 1) within current segment
    float currentValue_ = 0.0f;
    float curve_ = 0.0f;
    float curveExponent_ = 1.0f;    // 1 + |curve_| * kCurveK
    Stage stage_ = Stage::Idle;
    bool cycle_ = false;
    bool eocPending_ = false;

    static constexpr float kCurveK   = 2.0f;
    static constexpr float kMinTime  = 0.001f;
    static constexpr float kMaxRise  = 4.0f;
    static constexpr float kMaxFall  = 8.0f;

    float shape(float t) const;
    float inverseShape(float v) const;
};

} // namespace ideath
