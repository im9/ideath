#pragma once

#include <cstdint>

namespace ideath {

/// Low-frequency oscillator for modulation.
/// Supports sine, triangle, square, saw, and sample-and-hold waveforms.
/// Output can be unipolar [0, 1] or bipolar [-1, 1].
class LFO
{
public:
    enum class Waveform { Sine, Triangle, Square, Saw, SampleAndHold };
    enum class Polarity { Bipolar, Unipolar };

    LFO() = default;

    void prepare(float sampleRate);
    void reset();

    /// Set rate in Hz (typically 0.01 – 50).
    void setRate(float rateHz);
    void setWaveform(Waveform wf);
    void setPolarity(Polarity pol);

    /// Enable one-shot mode: runs one cycle then holds at end value.
    void setOneShot(bool enabled);

    // --- Shape / Curve / Quantize extensions ---
    // These are additive: at default values (0.0) the LFO is bit-equivalent
    // to the legacy single-waveform behaviour selected via setWaveform().

    /// Shape (0..1): mixes a second oscillator at a polyrhythmic frequency
    /// ratio (Lissajous-style). At 0 the LFO is single-oscillator.
    void setShape(float shape);

    /// Curve (0..1): continuous morph between sine → triangle → saw → square.
    /// 0.0 = sine, ~0.33 = triangle, ~0.66 = saw, 1.0 = square.
    /// When set to a non-default value this overrides the legacy `Waveform`
    /// selection (the morph drives the carrier shape directly).
    void setCurve(float curve);

    /// Quantize (0..1): mixes between the smooth output and a sample-and-hold
    /// version that updates once per LFO cycle. 0 = smooth, 1 = stepped.
    void setQuantize(float quantize);

    /// Retrigger from phase 0 (useful for synced or one-shot use).
    void trigger();

    float process();

    float getPhase() const { return phase_; }
    bool isFinished() const { return finished_; }

private:
    float sampleRate_ = 44100.0f;
    float phase_ = 0.0f;
    float phase2_ = 0.0f;        // second oscillator phase (for Shape)
    float phaseInc_ = 0.0f;
    Waveform waveform_ = Waveform::Sine;
    Polarity polarity_ = Polarity::Bipolar;
    bool oneShot_ = false;
    bool finished_ = false;
    float holdValue_ = 0.0f;     // for S&H (legacy waveform path)
    float quantizeHold_ = 0.0f;  // S&H value used by Quantize parameter
    float prevPhase_ = 0.0f;     // to detect phase wrap for S&H
    uint32_t noiseState_ = 0x12345678u; // for S&H random

    float shape_ = 0.0f;
    float curve_ = 0.0f;
    float quantize_ = 0.0f;
};

} // namespace ideath
