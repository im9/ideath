#pragma once

#include <ideath/Oscillator.h>
#include <vector>

namespace ideath {

/// Stacked detuned oscillators for unison/supersaw effects.
/// Spreads N voices symmetrically around the center frequency.
class UnisonOscillator
{
public:
    static constexpr int kMaxVoices = 16;

    UnisonOscillator() = default;

    void prepare(float sampleRate);
    void reset();

    /// Set number of unison voices (clamped to [1, kMaxVoices]).
    void setVoiceCount(int count);

    /// Set center frequency in Hz.
    void setFrequency(float freqHz);

    /// Set detune spread in cents (total spread, symmetric around center).
    void setDetune(float cents);

    /// Set analog-style pitch drift amount in cents (peak deviation).
    /// Each voice gets its own slow LFO. Default 0 = drift disabled
    /// (output bit-identical to non-drift behavior).
    void setDriftAmount(float cents);

    /// Set base drift LFO rate in Hz. Each voice runs at a slightly
    /// different rate to keep voices uncorrelated. Default 0.3 Hz.
    void setDriftRate(float hz);

    /// Process one sample. waveform: 0.0 = square, 1.0 = saw.
    /// Returns the mixed (gain-compensated) output.
    float process(float waveform = 1.0f);

    int getVoiceCount() const { return voiceCount_; }

    /// Test/debug: instantaneous drift offset (in cents) currently
    /// applied to voice `i`. Zero when drift is disabled.
    float getVoiceDriftCents(int i) const;

private:
    void updateFrequencies();

    float sampleRate_ = 44100.0f;
    float frequency_ = 440.0f;
    float detuneCents_ = 10.0f;
    int voiceCount_ = 1;
    Oscillator voices_[kMaxVoices];

    // Per-voice base frequency (after detune, before drift). Cached so
    // the per-sample drift loop does not have to recompute pow().
    float baseVoiceFreq_[kMaxVoices] = {};

    // Analog drift state (per-voice slow LFO, sine).
    float driftAmountCents_ = 0.0f;
    float driftRateHz_ = 0.3f;
    float driftPhase_[kMaxVoices] = {};
    float driftPhaseInc_[kMaxVoices] = {};
};

} // namespace ideath
