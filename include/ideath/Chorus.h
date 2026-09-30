#pragma once

#include <vector>

namespace ideath {

/// Multi-voice bucket-brigade chorus — LFO-modulated fractional delay lines
/// summed into a single wet bus, then blended with dry.
///
/// The "breathing detune" character that separates a chorus from `UnisonOscillator`
/// (static multi-voice detune → thickness) is the *time-varying* pitch wobble
/// produced by the fractional delay modulation.  Feed a pad through this to
/// hear the ambient movement.
///
/// Signal chain per voice i:
///     write   = in + Σ voice_wet · feedback
///     read_i  = write buffer at (base_ms + depth_ms · sin(2π (φ + i·spread/N)))
/// Wet bus is the mean of N voices; final output = (1−mix)·in + mix·wet.
///
/// For flanger-flavoured tone (comb-filter sweep) drop the base delay
/// via a short `setDepth` and set `setFeedback` negative — the same
/// building block yields a distinct sonic character.
///
/// Peak output ceiling ±1.3 (mean of N voices bounded by ±1 for ±1 input,
/// plus feedback headroom + mix overshoot).  Denormal-guarded feedback path.
class Chorus
{
public:
    /// Maximum number of chorus voices — sized once at construction to keep
    /// `process()` allocation-free.  Larger than the typical ambient-pad 3-voice
    /// setup so plugin layer has room for wider ensembles.
    static constexpr int kMaxVoices = 8;

    Chorus() = default;

    void prepare(float sampleRate);
    void reset();

    /// Voice count.  Clamped to [1, kMaxVoices].  Default 3.
    void setVoiceCount(int n);

    /// LFO rate in Hz.  Clamped to [0.01, 20].  Default 0.5.
    void setRate(float hz);

    /// Delay-modulation depth in milliseconds.  Clamped to [0, 15].
    /// The instantaneous delay stays within [base − depth, base + depth]
    /// around a fixed ~12 ms base — ambient-chorus territory, not flanger.
    /// For flanger tone use small depth + negative feedback.  Default 3 ms.
    void setDepth(float milliseconds);

    /// Voice-to-voice LFO phase offset amount, [0, 1].  0 = all voices in
    /// lockstep (identical modulation → chorus collapses to a single delayed
    /// copy); 1 = voices spread evenly across the LFO cycle (max stereo
    /// decorrelation + spectral movement).  Default 0.5.
    void setSpread(float amount);

    /// Feedback amount, clamped to [-0.9, 0.9].  Negative values give the
    /// signature flanger comb-filter tone.  Default 0.
    void setFeedback(float amount);

    /// Dry/wet mix, clamped to [0, 1].  Default 0.5.
    void setMix(float wet);

    /// Sum of N voices, mono.
    float processMono(float in);

    /// Stereo output: voices are panned across L/R by voice index so LFO
    /// phase spread produces channel decorrelation.  Convenient wrapper —
    /// internally shares one write pass with `processMono` semantics.
    void processStereo(float in, float& outL, float& outR);

    /// Convenience alias for the mono path.
    float process(float in) { return processMono(in); }

    int getVoiceCount() const { return voiceCount_; }

private:
    /// Fractional read from the shared write buffer, `delaySamples` back from
    /// the current `writePos_`.  Linear interpolation.
    float readAt(float delaySamples) const;

    /// Compute the per-voice modulated delay in samples for voice `i` at the
    /// current LFO phase.  Extracted so `processMono` and `processStereo` share
    /// the same delay math.
    float voiceDelay(int i) const;

    float sampleRate_        = 44100.0f;
    std::vector<float> buffer_;
    int bufferSize_          = 0;
    int writePos_            = 0;

    int   voiceCount_        = 3;
    float rateHz_            = 0.5f;
    float phaseInc_          = 0.0f;
    float depthSamples_      = 132.0f;   // ~3 ms @ 44100
    float baseDelaySamples_  = 529.0f;   // ~12 ms @ 44100
    float spread_            = 0.5f;
    float feedback_          = 0.0f;
    float mix_               = 0.5f;
    float phase_             = 0.0f;     // master LFO phase [0, 1)
};

} // namespace ideath
