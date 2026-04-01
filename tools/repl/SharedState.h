#pragma once

#include <atomic>
#include <cstdint>

namespace ideath { namespace repl {

static constexpr int kMaxSeqSteps = 64;

enum class SourceType { Oscillator, Wavetable, Noise, None };
enum class OscWaveform { Saw, Square };
enum class WtShape { Square, Saw, Triangle, Sine };
enum class FilterType { Off, Lowpass, Highpass, Bandpass };
enum class LfoTarget { Off, Pitch, Filter, Volume };
enum class LfoWaveform { Sine, Triangle, Square, Saw, SampleAndHold };

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

    // LFO
    LfoTarget lfoTarget = LfoTarget::Off;
    LfoWaveform lfoWaveform = LfoWaveform::Sine;
    float lfoRate = 5.0f;
    float lfoDepth = 0.0f;

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
