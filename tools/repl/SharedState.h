#pragma once

#include <atomic>
#include <cstdint>

namespace ideath { namespace repl {

static constexpr int kMaxSeqSteps = 64;

enum class SourceType { Oscillator, Wavetable, Noise, FM, Unison, KarplusStrong, Modal, None };
enum class OscWaveform { Saw, Square };
enum class WtShape { Square, Saw, Triangle, Sine };
enum class FilterType { Off, Lowpass, Highpass, Bandpass };
enum class LfoTarget { Off, Pitch, Filter, Volume };
enum class LfoWaveform { Sine, Triangle, Square, Saw, SampleAndHold };
enum class FgTarget { Off, Pitch, Filter, Volume };
enum class ReverbType { Off, Room, Hall, Shimmer };

struct VoiceParams
{
    // Source
    SourceType source = SourceType::None;
    OscWaveform oscWaveform = OscWaveform::Saw;
    WtShape wtShape = WtShape::Square;
    float frequency = 440.0f;

    // Envelope
    float attack = 0.01f;
    float decay = 0.1f;
    float sustain = 0.7f;
    float release = 0.3f;
    bool envelopeEnabled = false;

    // Filter
    FilterType filterType = FilterType::Off;
    float filterFreq = 1000.0f;
    float filterQ = 0.707f;

    // BitCrusher
    bool crushEnabled = false;
    int crushBits = 8;
    float crushRate = 22050.0f;

    // Saturation
    bool satEnabled = false;
    float satDrive = 1.0f;

    // Delay
    bool delayEnabled = false;
    float delayTime = 0.3f;
    float delayFeedback = 0.3f;

    // TapeDelay
    bool tapeDelayEnabled = false;
    float tapeDelayTime = 0.35f;
    float tapeDelayFeedback = 0.45f;
    float tapeDelayWowDepth = 0.002f;
    float tapeDelayWowRate = 0.3f;
    float tapeDelayFlutterDepth = 0.0007f;
    float tapeDelayFlutterRate = 4.0f;
    float tapeDelayLowpass = 6000.0f;
    float tapeDelayHighpass = 80.0f;
    float tapeDelayDrive = 2.0f;

    // LFO
    LfoTarget lfoTarget = LfoTarget::Off;
    LfoWaveform lfoWaveform = LfoWaveform::Sine;
    float lfoRate = 5.0f;
    float lfoDepth = 0.0f;

    // Function generator (West Coast rise/fall envelope, see FunctionGenerator.h)
    // Pitch / filter depth is in cents (output is unipolar [0,1] * depth).
    // Volume depth is percent (sample *= 1 + fgVal * depth/100).
    FgTarget fgTarget = FgTarget::Off;
    bool fgCycle = false;
    float fgRise = 0.1f;
    float fgFall = 0.5f;
    float fgCurve = 0.0f;
    float fgDepth = 0.0f;

    // FM Synth
    int fmAlgorithm = 0;
    float fmRatios[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float fmLevels[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float fmFeedback[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    // Compressor
    bool compEnabled = false;
    float compThreshold = -20.0f;
    float compRatio = 4.0f;
    float compAttack = 0.01f;
    float compRelease = 0.1f;
    float compMakeup = 0.0f;

    // Reverb
    ReverbType reverbType = ReverbType::Off;
    float reverbSize = 0.5f;
    float reverbDamp = 0.3f;
    float reverbMix = 0.3f;
    bool reverbFreeze = false;
    // Hall-specific
    float reverbPreDelay = 0.03f;
    float reverbModDepth = 0.5f;
    // Shimmer-specific
    float reverbShimmer = 0.5f;

    // Wavefolder
    bool foldEnabled = false;
    float foldDrive = 1.0f;
    float foldMix = 1.0f;

    // CombFilter
    bool combEnabled = false;
    float combDelay = 0.01f;
    float combFeedback = 0.8f;
    float combDamp = 0.3f;
    float combMix = 1.0f;

    // Unison oscillator
    int unisonVoices = 5;
    float unisonDetune = 15.0f;

    // Karplus-Strong plucked string
    float ksDecay   = 1.0f;
    float ksDamping = 0.3f;
    float ksExciter = 1.0f;

    // ModalResonator (bell engine).  Fundamental tracks `frequency` like
    // every other source; partial count / decay / inharmonicity are exposed
    // as a single sweep via `modal` REPL command.  `strike()` is fired on
    // every note-on / sequencer step automatically.
    int   modalPartials      = 8;
    float modalDecay         = 0.7f;
    float modalInharmonicity = 0.0f;

    // GranularProcessor (input-consuming, like delay)
    bool  granularEnabled = false;
    float granularRate = 40.0f;          // grains per second
    float granularSize = 0.08f;          // seconds per grain
    float granularPitchSpread = 0.0f;    // ±semitones
    float granularScatter = 0.5f;        // [0, 1]
    float granularMix = 0.5f;            // dry/wet
    bool  granularFreeze = false;

    // Looper (FeedbackBuffer)
    enum class LoopAction { None, Record, Stop, Play, Overdub, Off };
    LoopAction loopAction = LoopAction::None;
    float loopFeedback = 0.8f;
    float loopMix = 1.0f;

    // Pitch envelope (for kicks/percussion)
    bool pitchEnvEnabled = false;
    float pitchEnvAmount = 24.0f;  // semitones
    float pitchEnvDecay = 0.05f;   // seconds

    // Portamento
    float portaTime = 0.0f;

    // Master
    float volume = 0.5f;
};

struct SequencerState
{
    float frequencies[kMaxSeqSteps] = {}; // 0 = rest
    float velocities[kMaxSeqSteps] = {};  // 0.0-1.0 per step
    int numSteps = 0;
    float bpm = 120.0f;
    float gatePercent = 80.0f;            // 1-100
    bool running = false;
};

struct SharedState
{
    VoiceParams staging;
    std::atomic<bool> paramsReady{false};
    std::atomic<int> noteOnCounter{0};
    std::atomic<int> noteOffCounter{0};
    std::atomic<bool> stopRequested{false};

    SequencerState seqStaging;
    std::atomic<bool> seqReady{false};
};

}} // namespace ideath::repl
