#include "Presets.h"

namespace ideath { namespace repl {

static VoiceParams makeAcid()
{
    VoiceParams p;
    p.source = SourceType::Oscillator;
    p.oscWaveform = OscWaveform::Saw;
    p.envelopeEnabled = true;
    p.attack = 0.005f;
    p.decay = 0.15f;
    p.sustain = 0.0f;
    p.release = 0.05f;
    p.filterType = FilterType::Lowpass;
    p.filterFreq = 400.0f;
    p.filterQ = 8.0f;
    p.satEnabled = true;
    p.satDrive = 2.0f;
    return p;
}

static VoiceParams makeChiptune()
{
    VoiceParams p;
    p.source = SourceType::Wavetable;
    p.wtShape = WtShape::Square;
    p.envelopeEnabled = true;
    p.attack = 0.001f;
    p.decay = 0.05f;
    p.sustain = 0.0f;
    p.release = 0.02f;
    p.crushEnabled = true;
    p.crushBits = 4;
    p.crushRate = 8000.0f;
    return p;
}

static VoiceParams makePad()
{
    VoiceParams p;
    p.source = SourceType::Wavetable;
    p.wtShape = WtShape::Sine;
    p.envelopeEnabled = true;
    p.attack = 0.8f;
    p.decay = 0.5f;
    p.sustain = 0.7f;
    p.release = 1.0f;
    p.delayEnabled = true;
    p.delayTime = 0.4f;
    p.delayFeedback = 0.4f;
    return p;
}

static VoiceParams makeKick()
{
    VoiceParams p;
    p.source = SourceType::Wavetable;
    p.wtShape = WtShape::Sine;
    p.frequency = 42.0f;
    p.envelopeEnabled = true;
    p.attack = 0.001f;
    p.decay = 0.15f;
    p.sustain = 0.0f;
    p.release = 0.03f;
    p.pitchEnvEnabled = true;
    p.pitchEnvAmount = 24.0f;  // 2 octaves sweep (less clicky)
    p.pitchEnvDecay = 0.03f;   // 30ms pitch drop (tight)
    p.satEnabled = true;
    p.satDrive = 3.0f;         // more harmonics for body
    p.filterType = FilterType::Lowpass;
    p.filterFreq = 120.0f;     // keep it subby
    p.filterQ = 1.5f;
    return p;
}

static VoiceParams makePerc()
{
    VoiceParams p;
    p.source = SourceType::Noise;
    p.envelopeEnabled = true;
    p.attack = 0.001f;
    p.decay = 0.08f;
    p.sustain = 0.0f;
    p.release = 0.02f;
    p.filterType = FilterType::Highpass;
    p.filterFreq = 2000.0f;
    p.filterQ = 1.0f;
    return p;
}

static VoiceParams makeBass()
{
    VoiceParams p;
    p.source = SourceType::Oscillator;
    p.oscWaveform = OscWaveform::Square;
    p.frequency = 55.0f;
    p.envelopeEnabled = true;
    p.attack = 0.005f;
    p.decay = 0.2f;
    p.sustain = 0.6f;
    p.release = 0.1f;
    p.filterType = FilterType::Lowpass;
    p.filterFreq = 600.0f;
    p.filterQ = 2.0f;
    return p;
}

static VoiceParams makeLead()
{
    VoiceParams p;
    p.source = SourceType::Wavetable;
    p.wtShape = WtShape::Saw;
    p.envelopeEnabled = true;
    p.attack = 0.01f;
    p.decay = 0.1f;
    p.sustain = 0.8f;
    p.release = 0.2f;
    p.filterType = FilterType::Lowpass;
    p.filterFreq = 3000.0f;
    p.filterQ = 1.5f;
    p.portaTime = 0.05f;
    return p;
}

static VoiceParams makeHihat()
{
    VoiceParams p;
    p.source = SourceType::Noise;
    p.envelopeEnabled = true;
    p.attack = 0.001f;
    p.decay = 0.03f;
    p.sustain = 0.0f;
    p.release = 0.01f;
    p.filterType = FilterType::Highpass;
    p.filterFreq = 8000.0f;
    p.filterQ = 1.0f;
    return p;
}

static VoiceParams makeAmbient()
{
    VoiceParams p;
    p.source = SourceType::Wavetable;
    p.wtShape = WtShape::Triangle;
    p.envelopeEnabled = true;
    p.attack = 1.5f;
    p.decay = 1.0f;
    p.sustain = 0.5f;
    p.release = 2.0f;
    p.delayEnabled = true;
    p.delayTime = 0.6f;
    p.delayFeedback = 0.6f;
    p.filterType = FilterType::Lowpass;
    p.filterFreq = 2000.0f;
    p.filterQ = 0.707f;
    return p;
}

static VoiceParams makeLofi()
{
    VoiceParams p;
    p.source = SourceType::Wavetable;
    p.wtShape = WtShape::Sine;
    p.envelopeEnabled = true;
    p.attack = 0.01f;
    p.decay = 0.2f;
    p.sustain = 0.5f;
    p.release = 0.3f;
    p.crushEnabled = true;
    p.crushBits = 8;
    p.crushRate = 11025.0f;
    p.satEnabled = true;
    p.satDrive = 1.5f;
    return p;
}

static const std::vector<PresetEntry> kPresets = {
    { "acid",     "303-style acid (saw + LP + resonance + sat)",     makeAcid()     },
    { "chiptune", "8-bit chiptune (square wt + bitcrusher)",         makeChiptune() },
    { "pad",      "soft pad (sine wt + slow ADSR + delay)",          makePad()      },
    { "kick",     "bass drum (sine + pitch sweep + saturation)",      makeKick()     },
    { "perc",     "percussive hit (noise + fast decay + HP)",        makePerc()     },
    { "bass",     "sub bass (square + LP + medium decay)",           makeBass()     },
    { "lead",     "lead synth (saw wt + LP + portamento)",           makeLead()     },
    { "hihat",    "hi-hat (noise + very fast decay + HP)",           makeHihat()    },
    { "ambient",  "ambient (triangle wt + slow ADSR + delay + LP)",  makeAmbient()  },
    { "lofi",     "lo-fi (sine wt + bitcrusher + saturation)",       makeLofi()     },
};

const PresetEntry* getPreset(const std::string& name)
{
    for (const auto& entry : kPresets)
    {
        if (name == entry.name)
            return &entry;
    }
    return nullptr;
}

const std::vector<PresetEntry>& getAllPresets()
{
    return kPresets;
}

}} // namespace ideath::repl
