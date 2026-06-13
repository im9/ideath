#pragma once

#include "SharedState.h"
#include <ideath/Oscillator.h>
#include <ideath/Wavetable.h>
#include <ideath/Noise.h>
#include <ideath/Envelope.h>
#include <ideath/SVFilter.h>
#include <ideath/BitCrusher.h>
#include <ideath/Saturation.h>
#include <ideath/DelayLine.h>
#include <ideath/TapeDelay.h>
#include <ideath/CombFilter.h>
#include <ideath/LFO.h>
#include <ideath/FunctionGenerator.h>
#include <ideath/Portamento.h>
#include <ideath/FMSynth.h>
#include <ideath/Compressor.h>
#include <ideath/Reverb.h>
#include <ideath/HallReverb.h>
#include <ideath/ShimmerReverb.h>
#include <ideath/Wavefolder.h>
#include <ideath/UnisonOscillator.h>
#include <ideath/FeedbackBuffer.h>
#include <ideath/KarplusStrong.h>
#include <ideath/ModalResonator.h>
#include <ideath/GranularProcessor.h>
#include <ideath/HarmonicOscillator.h>
#include <ideath/BowedString.h>
#include <ideath/LowPassGateVoice.h>

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
    SVFilter filter_;
    BitCrusher crush_;
    DelayLine delay_;
    TapeDelay tapeDelay_;
    CombFilter comb_;
    LFO lfo_;
    FunctionGenerator fg_;
    Portamento porta_;
    FMSynth fm_;
    Compressor comp_;
    Reverb reverb_;
    HallReverb hallReverb_;
    ShimmerReverb shimmerReverb_;
    Wavefolder wavefolder_;
    UnisonOscillator unison_;
    FeedbackBuffer looper_;
    KarplusStrong karplus_;
    ModalResonator modal_;
    GranularProcessor granular_;
    HarmonicOscillator harmonic_;
    BowedString bowed_;
    LowPassGateVoice ping_;

    DecayEnvelope pitchEnv_;
    float baseFreq_ = 440.0f;
    bool stopped_ = true;
    bool delayCleared_ = false;
    Portamento gainSmoother_;

    // Sequencer state (audio-thread local)
    SequencerState seq_;
    int seqStep_ = 0;
    int seqSampleCounter_ = 0;
    int seqSamplesPerStep_ = 0;
    int seqGateSamples_ = 0;
    bool seqGateOpen_ = false;
    float seqVelocity_ = 1.0f;

    void advanceSequencer();
};

}} // namespace ideath::repl
