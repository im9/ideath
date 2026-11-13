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

    /// The drift LFO is evaluated every this many samples and each voice's
    /// frequency ratio is interpolated linearly in between (one sin + pow
    /// per voice per interval instead of per sample).  32 samples: at the
    /// fastest jittered rate a typical drift uses (5 Hz × 1.241, 48 kHz)
    /// the interpolation error is amount·(2π·f·32/sr)²/8 = 8e−5·amount
    /// cents — 0.001 cents at 10 cents of drift, far below the ≈ 3–5 cent
    /// pitch JND.
    static constexpr int kDriftControlInterval = 32;

    UnisonOscillator() = default;

    void prepare(float sampleRate);
    void reset();

    /// Set number of unison voices (clamped to [1, kMaxVoices]).
    void setVoiceCount(int count);

    /// Set center frequency in Hz.
    void setFrequency(float freqHz);

    /// Set detune spread in cents (total spread, symmetric around center).
    /// Returns at once when the clamped value equals the current one, so it
    /// is cheap to call every sample.
    void setDetune(float cents);

    /// Set analog-style pitch drift amount in cents (peak deviation).
    /// Each voice gets its own slow LFO, evaluated at control rate (see
    /// kDriftControlInterval).  Default 0 = drift disabled (output
    /// bit-identical to non-drift behavior); setting 0 puts every voice
    /// back on its undrifted frequency at once.
    void setDriftAmount(float cents);

    /// Set base drift LFO rate in Hz. Each voice runs at a slightly
    /// different rate to keep voices uncorrelated. Default 0.3 Hz.
    void setDriftRate(float hz);

    /// Process one sample. waveform: 0.0 = square, 1.0 = saw.
    /// Returns the mixed (gain-compensated) output.
    float process(float waveform = 1.0f);

    int getVoiceCount() const { return voiceCount_; }

    /// Representative phase of the unison stack (voice 0's phase in
    /// [0, 1)). Used as the Kuramoto consensus input when externally
    /// synchronising unison fundamental phases across instances.
    float getFundamentalPhase() const;

    /// Nudge every unison voice's phase by the same `delta` cycles.
    /// Preserves the detune spread (relative phase differences)
    /// while rotating the stack as a whole toward a Kuramoto target.
    void nudgeFundamentalPhase(float delta);

    /// Test/debug: drift offset (in cents) the next process() applies to
    /// voice `i`. Zero when drift is disabled.
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

    // Control-rate drift: ratio at the start of the current interval and
    // its per-sample step; driftPhase_ holds the phase at the interval's
    // end.  driftPos_ counts samples into the interval; driftInit_ forces
    // a fresh start (ratio from the current phase) at the next process().
    float driftRatio0_[kMaxVoices] = {};
    float driftRatioStep_[kMaxVoices] = {};
    int driftPos_ = 0;
    bool driftInit_ = true;
};

} // namespace ideath
