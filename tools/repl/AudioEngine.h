#pragma once

#include "SharedState.h"
#include <ideath/Oscillator.h>
#include <ideath/Wavetable.h>
#include <ideath/Noise.h>
#include <ideath/Envelope.h>
#include <ideath/Biquad.h>
#include <ideath/BitCrusher.h>
#include <ideath/Saturation.h>
#include <ideath/DelayLine.h>
#include <ideath/LFO.h>
#include <ideath/Portamento.h>

namespace ideath { namespace repl {

class AudioEngine
{
public:
    void prepare(float sampleRate);

    /// Called from audio thread at top of each buffer.
    void applyPendingState(SharedState& shared);

    /// Process one sample. Called from audio callback.
    float process();

private:
    float sampleRate_ = 44100.0f;

    // Current params (audio thread local copy)
    VoiceParams params_;
    int lastNoteOn_ = 0;
    int lastNoteOff_ = 0;

    // DSP primitives
    Oscillator osc_;
    Wavetable wt_;
    Noise noise_;
    AdsrEnvelope env_;
    Biquad filter_;
    BitCrusher crush_;
    DelayLine delay_;
    LFO lfo_;
    Portamento porta_;

    float baseFreq_ = 440.0f;
    bool stopped_ = true;
    bool delayCleared_ = false;
    Portamento gainSmoother_;
};

}} // namespace ideath::repl
