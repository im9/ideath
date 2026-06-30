#include "CommandParser.h"
#include "NoteUtils.h"
#include "Presets.h"
#include <sstream>
#include <vector>
#include <iostream>
#include <cctype>
#include <algorithm>
#include <random>

namespace ideath { namespace repl {

static std::vector<std::string> tokenize(const std::string& line)
{
    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string token;
    while (iss >> token)
        tokens.push_back(token);
    return tokens;
}

static float parseFloat(const std::string& s, float fallback)
{
    try { return std::stof(s); }
    catch (...) { return fallback; }
}

static int parseInt(const std::string& s, int fallback)
{
    try { return std::stoi(s); }
    catch (...) { return fallback; }
}

void printHelp()
{
    std::cout << R"(
ideath REPL commands:
  osc <saw|square> <freq>       Oscillator source
  wt <square|saw|tri|sine> <freq>  Wavetable source (Game Boy 4-bit, chiptune)
  multiwt <shape|pos> <freq>    MultiShapeWavetable source (inboil-style multi-
                                shape band-limited engine, mipmap anti-aliased).
                                Shape names: sine, square, saw, tri, pulse, super,
                                metal, spec, formA, formO. Numeric arg = morph
                                position 0-9 (fractional crossfades between shapes).
  noise                         Noise source
  fm <algo> [ratios] [levels]   FM synth source (YM2612 chiptune, algo 0-7)
  dxfm <algo 1-32> [r1:l1 ... r6:l6]
                                DXFMSynth source (DX7-style, 32 algorithms,
                                6 operators). Left-to-right: OP1, OP2, ... OP6.
  dxfb <amount>                 Feedback amount for dxfm (algorithm's FB op only)
  unison <saw|square> <freq> [voices] [detune_cents]
                                Unison oscillator source
  pluck [freq] [decay] [damp] [exciter]
                                Karplus-Strong plucked string source
  modal <fund> [partials] [decay] [inharm]
                                Bell engine (struck on note-on / seq step).
                                fund Hz, partials 1-16, decay sec, inharm 0-1.
  harmonic <fund> [low] [mid] [high] [shape] [partials]
                                Additive engine (Plaits-style sum of sines).
                                fund Hz, band levels 0-1 (LOW=harm 1-3,
                                MID=4-7, HIGH=8-32), shape 0-1 within-band
                                taper, partials 1-32 (CPU cap).
  bowed <fund> [pressure] [position] [damping]
                                Bowed-string physical model (continuous-
                                excitation, sibling of pluck). fund Hz,
                                pressure 0-1, position 0.02-0.5 (pickup),
                                damping 0-1 (sustain at 0 = 10s, at 1 = 0.1s).
                                Bow engages on note-on, releases on note-off.
  ping <fund> [tone] [damping] [brightness]
                                West Coast Low-Pass Gate (Buchla 292-style)
                                + saw/square carrier. fund Hz, tone 0-1
                                (0=square, 1=saw), damping 0-1 (LPG fall
                                80 ms → 600 ms), brightness 0-1 (peak
                                cutoff 50 Hz → 6 kHz). Re-pinged on every
                                note-on / sequencer step.
  filter <lp|hp|bp> <freq> <Q>  SVFilter (or "filter off")
  crush <bits> <rate>           BitCrusher (or "crush off")
  sat <drive>                   Saturation (or "sat off")
  fold <drive> [mix]            Wavefolder (or "fold off")
  delay <time> <feedback>       Delay line (or "delay off")
  tape <time> <feedback> [wowDepth] [wowRate] [flutterDepth] [flutterRate] [lp] [hp] [drive]
                                Tape delay (or "tape off")
  comb <time> <feedback> [damp] [mix]
                                Comb filter / resonator (or "comb off")
  granular <rate> <size> [pitchSpread] [scatter] [mix]
                                Granular cloud (or "granular off")
  granular freeze [on|off]      Freeze granular ring buffer
  loop <rec|stop|play|dub|off>  Looper (record, overdub, play)
  comp <thresh> <ratio> [attack] [release] [makeup]  Compressor (or "comp off")
  reverb <room|hall|shimmer> [params]  Reverb (or "reverb off")
  lfo <sine|tri|square|saw|sh> <rate> <pitch|filter|vol> <depth>
                                LFO modulation (or "lfo off")
  fg <rise> <fall> <curve> <oneshot|cycle> <pitch|filter|vol> <depth>
                                West Coast function generator (or "fg off").
                                Depth in cents for pitch/filter, percent for vol.
  env <A> <D> <S> <R>          Set ADSR envelope (or "env off")
  penv <semitones> <decay>    Pitch envelope (or "penv off")
  note <C4|freq>               Trigger note (with envelope if set)
  release                      Release note
  seq <notes...> [bpm]         Step sequencer (e.g. seq C4 E4 G4! 120)
  seq bpm <bpm>                Change sequencer tempo
  seq gate <percent>           Gate length (1-100, default 80)
  seq reverse                  Reverse pattern
  seq shuffle                  Shuffle pattern
  seq rotate [n]               Rotate pattern by n steps (default 1)
  seq stop                     Stop sequencer
  preset <name>                Load voice preset (or "preset list")
  limiter <threshold_dB>       Set limiter threshold (or "limiter off")
  scope                        Show oscilloscope snapshot (Braille)
  scope on                     Auto-refresh scope (every 500ms)
  scope off                    Stop scope
  track <n>                    Switch active track (1-8)
  track <n> mute|solo|vol <v>  Track mixing controls
  porta <time>                 Portamento time in seconds
  vol <0.0-1.0>                Master volume
  stop                         Silence and reset
  help                         Show this message
  quit / exit                  Exit
)" << std::endl;
}

bool parseCommand(const std::string& line, SharedState& shared)
{
    auto tokens = tokenize(line);
    if (tokens.empty()) return true;

    const auto& cmd = tokens[0];

    if (cmd == "quit" || cmd == "exit")
        return false;

    if (cmd == "help")
    {
        printHelp();
        return true;
    }

    if (cmd == "osc")
    {
        shared.staging.source = SourceType::Oscillator;
        if (tokens.size() > 1)
        {
            if (tokens[1] == "saw") shared.staging.oscWaveform = OscWaveform::Saw;
            else if (tokens[1] == "square") shared.staging.oscWaveform = OscWaveform::Square;
        }
        if (tokens.size() > 2)
            shared.staging.frequency = parseFloat(tokens[2], 440.0f);
        shared.paramsReady.store(true, std::memory_order_release);
        return true;
    }

    if (cmd == "wt")
    {
        shared.staging.source = SourceType::Wavetable;
        if (tokens.size() > 1)
        {
            if (tokens[1] == "square") shared.staging.wtShape = WtShape::Square;
            else if (tokens[1] == "saw") shared.staging.wtShape = WtShape::Saw;
            else if (tokens[1] == "tri") shared.staging.wtShape = WtShape::Triangle;
            else if (tokens[1] == "sine") shared.staging.wtShape = WtShape::Sine;
        }
        if (tokens.size() > 2)
            shared.staging.frequency = parseFloat(tokens[2], 440.0f);
        shared.paramsReady.store(true, std::memory_order_release);
        return true;
    }

    if (cmd == "multiwt")
    {
        // multiwt <shape_name | position> [freq]
        // Shape names map to MultiShapeWavetable::Shape enum positions.
        // Numeric arg → morph position (fractional crossfade between shapes).
        shared.staging.source = SourceType::MultiShape;
        if (tokens.size() > 1)
        {
            const std::string& a = tokens[1];
            float pos = -1.0f;
            if      (a == "sine")     pos = 0.0f;
            else if (a == "square")   pos = 1.0f;
            else if (a == "saw")      pos = 2.0f;
            else if (a == "tri")      pos = 3.0f;
            else if (a == "pulse")    pos = 4.0f;
            else if (a == "super")    pos = 5.0f;
            else if (a == "metal")    pos = 6.0f;
            else if (a == "spec")     pos = 7.0f;
            else if (a == "formA")    pos = 8.0f;
            else if (a == "formO")    pos = 9.0f;
            else                      pos = parseFloat(a, 0.0f); // numeric morph
            shared.staging.multiwtPosition = pos;
        }
        if (tokens.size() > 2)
            shared.staging.frequency = parseFloat(tokens[2], 440.0f);
        shared.paramsReady.store(true, std::memory_order_release);
        return true;
    }

    if (cmd == "noise")
    {
        shared.staging.source = SourceType::Noise;
        shared.paramsReady.store(true, std::memory_order_release);
        return true;
    }

    if (cmd == "fm")
    {
        shared.staging.source = SourceType::FM;
        if (tokens.size() > 1)
            shared.staging.fmAlgorithm = parseInt(tokens[1], 0);
        // fm <algo> <r1:l1> <r2:l2> <r3:l3> <r4:l4>
        // e.g. fm 0 1:1 2:0.5 3:0.3 4:0.2
        for (size_t i = 2; i < tokens.size() && (i - 2) < 4; ++i)
        {
            int op = static_cast<int>(i - 2);
            // Parse ratio:level or just ratio
            auto colonPos = tokens[i].find(':');
            if (colonPos != std::string::npos)
            {
                shared.staging.fmRatios[op] = parseFloat(tokens[i].substr(0, colonPos), 1.0f);
                shared.staging.fmLevels[op] = parseFloat(tokens[i].substr(colonPos + 1), 1.0f);
            }
            else
            {
                shared.staging.fmRatios[op] = parseFloat(tokens[i], 1.0f);
            }
        }
        shared.paramsReady.store(true, std::memory_order_release);
        return true;
    }

    if (cmd == "dxfm")
    {
        // dxfm <algo 1-32> [r1:l1] [r2:l2] ... [r6:l6]
        // Algorithm uses DX7 numbering (1-32) externally; stored internally
        // 0-indexed.  Operator order matches DX7 lingo (OP1..OP6 = op[5]..op[0]),
        // i.e. the first ratio:level slot maps to op[5] (typically a carrier).
        shared.staging.source = SourceType::DXFM;
        if (tokens.size() > 1)
        {
            int algoOneBased = parseInt(tokens[1], 32);
            int idx = std::clamp(algoOneBased - 1, 0, 31);
            shared.staging.dxfmAlgorithm = idx;
        }
        for (size_t i = 2; i < tokens.size() && (i - 2) < 6; ++i)
        {
            // op[5] corresponds to DX7 OP1, op[4] to OP2, ... — but the
            // command surfaces it left-to-right as the user types.  We map
            // tokens[i] for i=2 → op[5], i=3 → op[4], ..., i=7 → op[0].
            int op = 5 - static_cast<int>(i - 2);
            auto colonPos = tokens[i].find(':');
            if (colonPos != std::string::npos)
            {
                shared.staging.dxfmRatios[op] = parseFloat(tokens[i].substr(0, colonPos), 1.0f);
                shared.staging.dxfmLevels[op] = parseFloat(tokens[i].substr(colonPos + 1), 1.0f);
            }
            else
            {
                shared.staging.dxfmRatios[op] = parseFloat(tokens[i], 1.0f);
            }
        }
        shared.paramsReady.store(true, std::memory_order_release);
        return true;
    }

    if (cmd == "dxfb")
    {
        // dxfb <amount 0-1>  — feedback for the DXFMSynth's algorithm-defined
        // feedback operator (only the FB op responds; others ignore it).
        if (tokens.size() > 1)
        {
            shared.staging.dxfmFeedback = std::clamp(parseFloat(tokens[1], 0.0f), 0.0f, 1.0f);
            shared.paramsReady.store(true, std::memory_order_release);
        }
        else
        {
            std::cout << "Usage: dxfb <amount 0-1>" << std::endl;
        }
        return true;
    }

    if (cmd == "fmfb")
    {
        // Set per-operator feedback: fmfb <op> <amount>
        if (tokens.size() > 2)
        {
            int op = parseInt(tokens[1], 0);
            if (op >= 0 && op < 4)
            {
                shared.staging.fmFeedback[op] = parseFloat(tokens[2], 0.0f);
                shared.paramsReady.store(true, std::memory_order_release);
            }
        }
        else
        {
            std::cout << "Usage: fmfb <op 0-3> <amount 0-1>" << std::endl;
        }
        return true;
    }

    if (cmd == "comp")
    {
        if (tokens.size() > 1 && tokens[1] == "off")
        {
            shared.staging.compEnabled = false;
            shared.paramsReady.store(true, std::memory_order_release);
            std::cout << "Compressor OFF" << std::endl;
            return true;
        }

        shared.staging.compEnabled = true;
        // comp <threshold> <ratio> [attack] [release] [makeup]
        if (tokens.size() > 1)
            shared.staging.compThreshold = parseFloat(tokens[1], -20.0f);
        if (tokens.size() > 2)
            shared.staging.compRatio = parseFloat(tokens[2], 4.0f);
        if (tokens.size() > 3)
            shared.staging.compAttack = parseFloat(tokens[3], 0.01f);
        if (tokens.size() > 4)
            shared.staging.compRelease = parseFloat(tokens[4], 0.1f);
        if (tokens.size() > 5)
            shared.staging.compMakeup = parseFloat(tokens[5], 0.0f);

        shared.paramsReady.store(true, std::memory_order_release);
        std::cout << "Compressor: thresh=" << shared.staging.compThreshold
                  << "dB ratio=" << shared.staging.compRatio
                  << " A=" << shared.staging.compAttack
                  << " R=" << shared.staging.compRelease
                  << " makeup=" << shared.staging.compMakeup << "dB"
                  << std::endl;
        return true;
    }

    if (cmd == "reverb")
    {
        if (tokens.size() > 1 && tokens[1] == "off")
        {
            shared.staging.reverbType = ReverbType::Off;
            shared.paramsReady.store(true, std::memory_order_release);
            return true;
        }

        if (tokens.size() > 1 && tokens[1] == "freeze")
        {
            shared.staging.reverbFreeze = !shared.staging.reverbFreeze;
            shared.paramsReady.store(true, std::memory_order_release);
            std::cout << "Reverb freeze: " << (shared.staging.reverbFreeze ? "ON" : "OFF") << std::endl;
            return true;
        }

        if (tokens.size() > 1)
        {
            if (tokens[1] == "room") shared.staging.reverbType = ReverbType::Room;
            else if (tokens[1] == "hall") shared.staging.reverbType = ReverbType::Hall;
            else if (tokens[1] == "shimmer") shared.staging.reverbType = ReverbType::Shimmer;
        }

        // Common params: reverb <type> [size] [damp] [mix]
        if (tokens.size() > 2)
            shared.staging.reverbSize = parseFloat(tokens[2], 0.5f);
        if (tokens.size() > 3)
            shared.staging.reverbDamp = parseFloat(tokens[3], 0.3f);
        if (tokens.size() > 4)
            shared.staging.reverbMix = parseFloat(tokens[4], 0.3f);

        // Hall-specific: reverb hall <size> <damp> <mix> <predelay> <moddepth>
        if (shared.staging.reverbType == ReverbType::Hall)
        {
            if (tokens.size() > 5)
                shared.staging.reverbPreDelay = parseFloat(tokens[5], 0.03f);
            if (tokens.size() > 6)
                shared.staging.reverbModDepth = parseFloat(tokens[6], 0.5f);
        }

        // Shimmer-specific: reverb shimmer <size> <damp> <mix> <shimmer_amount>
        if (shared.staging.reverbType == ReverbType::Shimmer)
        {
            if (tokens.size() > 5)
                shared.staging.reverbShimmer = parseFloat(tokens[5], 0.5f);
        }

        shared.paramsReady.store(true, std::memory_order_release);
        std::cout << "Reverb: ";
        switch (shared.staging.reverbType)
        {
            case ReverbType::Room:    std::cout << "room"; break;
            case ReverbType::Hall:    std::cout << "hall"; break;
            case ReverbType::Shimmer: std::cout << "shimmer"; break;
            default: break;
        }
        std::cout << " size=" << shared.staging.reverbSize
                  << " damp=" << shared.staging.reverbDamp
                  << " mix=" << shared.staging.reverbMix
                  << std::endl;
        return true;
    }

    if (cmd == "filter")
    {
        if (tokens.size() > 1 && tokens[1] == "off")
        {
            shared.staging.filterType = FilterType::Off;
        }
        else
        {
            if (tokens.size() > 1)
            {
                if (tokens[1] == "lp") shared.staging.filterType = FilterType::Lowpass;
                else if (tokens[1] == "hp") shared.staging.filterType = FilterType::Highpass;
                else if (tokens[1] == "bp") shared.staging.filterType = FilterType::Bandpass;
            }
            if (tokens.size() > 2)
                shared.staging.filterFreq = parseFloat(tokens[2], 1000.0f);
            if (tokens.size() > 3)
                shared.staging.filterQ = parseFloat(tokens[3], 0.707f);
        }
        shared.paramsReady.store(true, std::memory_order_release);
        return true;
    }

    if (cmd == "crush")
    {
        if (tokens.size() > 1 && tokens[1] == "off")
        {
            shared.staging.crushEnabled = false;
        }
        else
        {
            shared.staging.crushEnabled = true;
            if (tokens.size() > 1)
                shared.staging.crushBits = parseInt(tokens[1], 8);
            if (tokens.size() > 2)
                shared.staging.crushRate = parseFloat(tokens[2], 22050.0f);
        }
        shared.paramsReady.store(true, std::memory_order_release);
        return true;
    }

    if (cmd == "sat")
    {
        if (tokens.size() > 1 && tokens[1] == "off")
        {
            shared.staging.satEnabled = false;
        }
        else
        {
            shared.staging.satEnabled = true;
            if (tokens.size() > 1)
                shared.staging.satDrive = parseFloat(tokens[1], 1.0f);
        }
        shared.paramsReady.store(true, std::memory_order_release);
        return true;
    }

    if (cmd == "delay")
    {
        if (tokens.size() > 1 && tokens[1] == "off")
        {
            shared.staging.delayEnabled = false;
        }
        else
        {
            shared.staging.delayEnabled = true;
            if (tokens.size() > 1)
                shared.staging.delayTime = parseFloat(tokens[1], 0.3f);
            if (tokens.size() > 2)
                shared.staging.delayFeedback = parseFloat(tokens[2], 0.3f);
        }
        shared.paramsReady.store(true, std::memory_order_release);
        return true;
    }

    if (cmd == "tape")
    {
        if (tokens.size() > 1 && tokens[1] == "off")
        {
            shared.staging.tapeDelayEnabled = false;
        }
        else
        {
            shared.staging.tapeDelayEnabled = true;
            if (tokens.size() > 1)
                shared.staging.tapeDelayTime = parseFloat(tokens[1], 0.35f);
            if (tokens.size() > 2)
                shared.staging.tapeDelayFeedback = parseFloat(tokens[2], 0.45f);
            if (tokens.size() > 3)
                shared.staging.tapeDelayWowDepth = parseFloat(tokens[3], 0.002f);
            if (tokens.size() > 4)
                shared.staging.tapeDelayWowRate = parseFloat(tokens[4], 0.3f);
            if (tokens.size() > 5)
                shared.staging.tapeDelayFlutterDepth = parseFloat(tokens[5], 0.0007f);
            if (tokens.size() > 6)
                shared.staging.tapeDelayFlutterRate = parseFloat(tokens[6], 4.0f);
            if (tokens.size() > 7)
                shared.staging.tapeDelayLowpass = parseFloat(tokens[7], 6000.0f);
            if (tokens.size() > 8)
                shared.staging.tapeDelayHighpass = parseFloat(tokens[8], 80.0f);
            if (tokens.size() > 9)
                shared.staging.tapeDelayDrive = parseFloat(tokens[9], 2.0f);
        }
        shared.paramsReady.store(true, std::memory_order_release);
        return true;
    }

    if (cmd == "comb")
    {
        if (tokens.size() > 1 && tokens[1] == "off")
        {
            shared.staging.combEnabled = false;
        }
        else
        {
            shared.staging.combEnabled = true;
            if (tokens.size() > 1)
                shared.staging.combDelay = parseFloat(tokens[1], 0.01f);
            if (tokens.size() > 2)
                shared.staging.combFeedback = parseFloat(tokens[2], 0.8f);
            if (tokens.size() > 3)
                shared.staging.combDamp = parseFloat(tokens[3], 0.3f);
            if (tokens.size() > 4)
                shared.staging.combMix = parseFloat(tokens[4], 1.0f);
        }
        shared.paramsReady.store(true, std::memory_order_release);
        return true;
    }

    if (cmd == "granular")
    {
        // granular off                                  → disable
        // granular freeze [on|off]                      → toggle / set freeze
        // granular <rate> <size> [pitchSpread] [scatter] [mix]
        if (tokens.size() > 1 && tokens[1] == "off")
        {
            shared.staging.granularEnabled = false;
        }
        else if (tokens.size() > 1 && tokens[1] == "freeze")
        {
            if (tokens.size() > 2 && tokens[2] == "off")
                shared.staging.granularFreeze = false;
            else if (tokens.size() > 2 && tokens[2] == "on")
                shared.staging.granularFreeze = true;
            else
                shared.staging.granularFreeze = !shared.staging.granularFreeze;
        }
        else
        {
            shared.staging.granularEnabled = true;
            if (tokens.size() > 1)
                shared.staging.granularRate = parseFloat(tokens[1], 40.0f);
            if (tokens.size() > 2)
                shared.staging.granularSize = parseFloat(tokens[2], 0.08f);
            if (tokens.size() > 3)
                shared.staging.granularPitchSpread = parseFloat(tokens[3], 0.0f);
            if (tokens.size() > 4)
                shared.staging.granularScatter = parseFloat(tokens[4], 0.5f);
            if (tokens.size() > 5)
                shared.staging.granularMix = parseFloat(tokens[5], 0.5f);
        }
        shared.paramsReady.store(true, std::memory_order_release);
        return true;
    }

    if (cmd == "lfo")
    {
        if (tokens.size() > 1 && tokens[1] == "off")
        {
            shared.staging.lfoTarget = LfoTarget::Off;
        }
        else
        {
            if (tokens.size() > 1)
            {
                if (tokens[1] == "sine") shared.staging.lfoWaveform = LfoWaveform::Sine;
                else if (tokens[1] == "tri") shared.staging.lfoWaveform = LfoWaveform::Triangle;
                else if (tokens[1] == "square") shared.staging.lfoWaveform = LfoWaveform::Square;
                else if (tokens[1] == "saw") shared.staging.lfoWaveform = LfoWaveform::Saw;
                else if (tokens[1] == "sh") shared.staging.lfoWaveform = LfoWaveform::SampleAndHold;
            }
            if (tokens.size() > 2)
                shared.staging.lfoRate = parseFloat(tokens[2], 5.0f);
            if (tokens.size() > 3)
            {
                if (tokens[3] == "pitch") shared.staging.lfoTarget = LfoTarget::Pitch;
                else if (tokens[3] == "filter") shared.staging.lfoTarget = LfoTarget::Filter;
                else if (tokens[3] == "vol") shared.staging.lfoTarget = LfoTarget::Volume;
            }
            if (tokens.size() > 4)
                shared.staging.lfoDepth = parseFloat(tokens[4], 50.0f);
        }
        shared.paramsReady.store(true, std::memory_order_release);
        return true;
    }

    if (cmd == "fg")
    {
        if (tokens.size() > 1 && tokens[1] == "off")
        {
            shared.staging.fgTarget = FgTarget::Off;
        }
        else if (tokens.size() >= 7)
        {
            // fg <rise> <fall> <curve> <oneshot|cycle> <target> <depth>
            shared.staging.fgRise  = parseFloat(tokens[1], 0.1f);
            shared.staging.fgFall  = parseFloat(tokens[2], 0.5f);
            shared.staging.fgCurve = parseFloat(tokens[3], 0.0f);
            shared.staging.fgCycle = (tokens[4] == "cycle");
            if      (tokens[5] == "pitch")  shared.staging.fgTarget = FgTarget::Pitch;
            else if (tokens[5] == "filter") shared.staging.fgTarget = FgTarget::Filter;
            else if (tokens[5] == "vol")    shared.staging.fgTarget = FgTarget::Volume;
            else                            shared.staging.fgTarget = FgTarget::Off;
            shared.staging.fgDepth = parseFloat(tokens[6], 1200.0f);
        }
        else
        {
            std::cout << "fg: usage 'fg <rise> <fall> <curve> <oneshot|cycle> <pitch|filter|vol> <depth>' or 'fg off'" << std::endl;
        }
        shared.paramsReady.store(true, std::memory_order_release);
        return true;
    }

    if (cmd == "env")
    {
        if (tokens.size() > 1 && tokens[1] == "off")
        {
            shared.staging.envelopeEnabled = false;
        }
        else
        {
            shared.staging.envelopeEnabled = true;
            if (tokens.size() > 1)
                shared.staging.attack = parseFloat(tokens[1], 0.01f);
            if (tokens.size() > 2)
                shared.staging.decay = parseFloat(tokens[2], 0.1f);
            if (tokens.size() > 3)
                shared.staging.sustain = parseFloat(tokens[3], 0.7f);
            if (tokens.size() > 4)
                shared.staging.release = parseFloat(tokens[4], 0.3f);
        }
        shared.paramsReady.store(true, std::memory_order_release);
        return true;
    }

    if (cmd == "note")
    {
        if (tokens.size() > 1)
        {
            // Try as note name first, then as frequency
            float freq = noteToFreq(tokens[1]);
            if (freq <= 0.0f)
                freq = parseFloat(tokens[1], 440.0f);
            shared.staging.frequency = freq;
        }
        shared.paramsReady.store(true, std::memory_order_release);
        shared.noteOnCounter.fetch_add(1, std::memory_order_release);
        return true;
    }

    if (cmd == "release")
    {
        shared.noteOffCounter.fetch_add(1, std::memory_order_release);
        return true;
    }

    if (cmd == "penv")
    {
        if (tokens.size() > 1 && tokens[1] == "off")
        {
            shared.staging.pitchEnvEnabled = false;
            shared.paramsReady.store(true, std::memory_order_release);
            std::cout << "Pitch envelope OFF" << std::endl;
            return true;
        }

        shared.staging.pitchEnvEnabled = true;
        if (tokens.size() > 1)
            shared.staging.pitchEnvAmount = parseFloat(tokens[1], 24.0f);
        if (tokens.size() > 2)
            shared.staging.pitchEnvDecay = parseFloat(tokens[2], 0.05f);
        shared.paramsReady.store(true, std::memory_order_release);
        std::cout << "Pitch envelope: " << shared.staging.pitchEnvAmount
                  << " semitones, decay=" << shared.staging.pitchEnvDecay << "s" << std::endl;
        return true;
    }

    if (cmd == "porta")
    {
        if (tokens.size() > 1)
            shared.staging.portaTime = parseFloat(tokens[1], 0.0f);
        shared.paramsReady.store(true, std::memory_order_release);
        return true;
    }

    if (cmd == "vol")
    {
        if (tokens.size() > 1)
            shared.staging.volume = parseFloat(tokens[1], 0.5f);
        shared.paramsReady.store(true, std::memory_order_release);
        return true;
    }

    if (cmd == "preset")
    {
        if (tokens.size() < 2 || tokens[1] == "list")
        {
            std::cout << "Available presets:" << std::endl;
            for (const auto& entry : getAllPresets())
                std::cout << "  " << entry.name << "  — " << entry.description << std::endl;
            return true;
        }
        auto* entry = getPreset(tokens[1]);
        if (entry)
        {
            // Preserve current frequency and volume
            float freq = shared.staging.frequency;
            float vol = shared.staging.volume;
            shared.staging = entry->params;
            if (entry->params.frequency == 440.0f) // default; keep user's freq
                shared.staging.frequency = freq;
            shared.staging.volume = vol;
            shared.paramsReady.store(true, std::memory_order_release);
            std::cout << "Preset: " << entry->name << " (" << entry->description << ")" << std::endl;
        }
        else
        {
            std::cout << "Unknown preset: " << tokens[1] << ". Type 'preset list' for options." << std::endl;
        }
        return true;
    }

    if (cmd == "seq")
    {
        if (tokens.size() > 1 && tokens[1] == "stop")
        {
            shared.seqStaging.running = false;
            shared.seqReady.store(true, std::memory_order_release);
            std::cout << "Sequencer stopped." << std::endl;
            return true;
        }

        if (tokens.size() > 1 && tokens[1] == "bpm")
        {
            if (tokens.size() > 2)
            {
                shared.seqStaging.bpm = parseFloat(tokens[2], 120.0f);
                shared.seqStaging.running = true;
                shared.seqReady.store(true, std::memory_order_release);
                std::cout << "BPM: " << shared.seqStaging.bpm << std::endl;
            }
            return true;
        }

        if (tokens.size() > 1 && tokens[1] == "gate")
        {
            if (tokens.size() > 2)
            {
                shared.seqStaging.gatePercent = parseFloat(tokens[2], 80.0f);
                if (shared.seqStaging.gatePercent < 1.0f) shared.seqStaging.gatePercent = 1.0f;
                if (shared.seqStaging.gatePercent > 100.0f) shared.seqStaging.gatePercent = 100.0f;
                shared.seqStaging.running = true;
                shared.seqReady.store(true, std::memory_order_release);
                std::cout << "Gate: " << shared.seqStaging.gatePercent << "%" << std::endl;
            }
            return true;
        }

        if (tokens.size() > 1 && tokens[1] == "reverse")
        {
            int n = shared.seqStaging.numSteps;
            if (n > 1)
            {
                std::reverse(shared.seqStaging.frequencies, shared.seqStaging.frequencies + n);
                std::reverse(shared.seqStaging.velocities, shared.seqStaging.velocities + n);
                shared.seqStaging.running = true;
                shared.seqReady.store(true, std::memory_order_release);
                std::cout << "Sequence reversed." << std::endl;
            }
            return true;
        }

        if (tokens.size() > 1 && tokens[1] == "shuffle")
        {
            int n = shared.seqStaging.numSteps;
            if (n > 1)
            {
                static std::mt19937 rng{std::random_device{}()};
                // Fisher-Yates shuffle, keeping freq/vel pairs together
                for (int i = n - 1; i > 0; --i)
                {
                    std::uniform_int_distribution<int> dist(0, i);
                    int j = dist(rng);
                    std::swap(shared.seqStaging.frequencies[i], shared.seqStaging.frequencies[j]);
                    std::swap(shared.seqStaging.velocities[i], shared.seqStaging.velocities[j]);
                }
                shared.seqStaging.running = true;
                shared.seqReady.store(true, std::memory_order_release);
                std::cout << "Sequence shuffled." << std::endl;
            }
            return true;
        }

        if (tokens.size() > 1 && tokens[1] == "rotate")
        {
            int n = shared.seqStaging.numSteps;
            if (n > 1)
            {
                int amount = (tokens.size() > 2) ? parseInt(tokens[2], 1) : 1;
                amount = ((amount % n) + n) % n; // normalize
                std::rotate(shared.seqStaging.frequencies, shared.seqStaging.frequencies + amount, shared.seqStaging.frequencies + n);
                std::rotate(shared.seqStaging.velocities, shared.seqStaging.velocities + amount, shared.seqStaging.velocities + n);
                shared.seqStaging.running = true;
                shared.seqReady.store(true, std::memory_order_release);
                std::cout << "Sequence rotated by " << amount << "." << std::endl;
            }
            return true;
        }

        // seq <note1> <note2> ... [bpm]
        // Notes can end with ! for accent (velocity 1.0, normal = 0.7)
        if (tokens.size() < 2)
        {
            std::cout << "Usage: seq <note1> [note2!] ... [bpm]" << std::endl;
            return true;
        }

        // Collect notes and detect BPM (last token if purely numeric)
        float bpm = shared.seqStaging.bpm; // preserve current BPM
        size_t noteEnd = tokens.size();

        // Check if last token is a BPM value (purely numeric)
        const auto& lastTok = tokens.back();
        bool lastIsNumber = !lastTok.empty();
        for (char c : lastTok)
        {
            if (!std::isdigit(c) && c != '.')
            {
                lastIsNumber = false;
                break;
            }
        }
        // If last token is a number > 20, treat as BPM (notes like C4 contain letters)
        if (lastIsNumber)
        {
            float val = parseFloat(lastTok, 0.0f);
            if (val > 20.0f)
            {
                bpm = val;
                noteEnd = tokens.size() - 1;
            }
        }

        int numSteps = 0;
        for (size_t i = 1; i < noteEnd && numSteps < kMaxSeqSteps; ++i)
        {
            if (tokens[i] == "-" || tokens[i] == ".")
            {
                shared.seqStaging.frequencies[numSteps] = 0.0f; // rest
                shared.seqStaging.velocities[numSteps] = 0.0f;
                ++numSteps;
            }
            else
            {
                // Check for accent suffix '!'
                std::string noteStr = tokens[i];
                float velocity = 0.7f;
                if (!noteStr.empty() && noteStr.back() == '!')
                {
                    velocity = 1.0f;
                    noteStr.pop_back();
                }

                float freq = noteToFreq(noteStr);
                if (freq <= 0.0f)
                    freq = parseFloat(noteStr, 0.0f);
                if (freq > 0.0f)
                {
                    shared.seqStaging.frequencies[numSteps] = freq;
                    shared.seqStaging.velocities[numSteps] = velocity;
                    ++numSteps;
                }
                else
                    std::cout << "Skipping unknown note: " << tokens[i] << std::endl;
            }
        }

        if (numSteps > 0)
        {
            shared.seqStaging.numSteps = numSteps;
            shared.seqStaging.bpm = bpm;
            shared.seqStaging.gatePercent = shared.seqStaging.gatePercent > 0.0f ? shared.seqStaging.gatePercent : 80.0f;
            shared.seqStaging.running = true;
            shared.seqReady.store(true, std::memory_order_release);
            std::cout << "Sequencer: " << numSteps << " steps @ " << bpm << " BPM" << std::endl;
        }
        else
        {
            std::cout << "No valid notes in sequence." << std::endl;
        }
        return true;
    }

    if (cmd == "unison")
    {
        shared.staging.source = SourceType::Unison;
        if (tokens.size() > 1)
        {
            if (tokens[1] == "saw") shared.staging.oscWaveform = OscWaveform::Saw;
            else if (tokens[1] == "square") shared.staging.oscWaveform = OscWaveform::Square;
        }
        if (tokens.size() > 2)
            shared.staging.frequency = parseFloat(tokens[2], 440.0f);
        if (tokens.size() > 3)
            shared.staging.unisonVoices = parseInt(tokens[3], 5);
        if (tokens.size() > 4)
            shared.staging.unisonDetune = parseFloat(tokens[4], 15.0f);
        shared.paramsReady.store(true, std::memory_order_release);
        std::cout << "Unison: " << shared.staging.unisonVoices
                  << " voices, detune=" << shared.staging.unisonDetune << " cents" << std::endl;
        return true;
    }

    if (cmd == "pluck")
    {
        // pluck [freq] [decay_sec] [damping 0-1] [exciter 0-1]
        // Selects the Karplus-Strong source. Defaults match a comfortable
        // mid-range nylon-string-ish pluck.
        shared.staging.source = SourceType::KarplusStrong;
        if (tokens.size() > 1)
            shared.staging.frequency = parseFloat(tokens[1], 220.0f);
        if (tokens.size() > 2)
            shared.staging.ksDecay = parseFloat(tokens[2], 1.0f);
        if (tokens.size() > 3)
            shared.staging.ksDamping = parseFloat(tokens[3], 0.3f);
        if (tokens.size() > 4)
            shared.staging.ksExciter = parseFloat(tokens[4], 1.0f);
        shared.paramsReady.store(true, std::memory_order_release);
        std::cout << "Pluck (Karplus-Strong): freq=" << shared.staging.frequency
                  << " decay=" << shared.staging.ksDecay
                  << " damp=" << shared.staging.ksDamping
                  << " exciter=" << shared.staging.ksExciter << std::endl;
        return true;
    }

    if (cmd == "modal")
    {
        // modal <fund> [partials] [decay] [inharmonicity]
        // Bell engine: each note-on / sequencer step fires a strike() that
        // excites the modes; partial ratios are the harmonic series, fund
        // tracks the standard `frequency` field, decay is applied uniformly
        // to all partials, inharmonicity stretches upper partials.
        shared.staging.source = SourceType::Modal;
        if (tokens.size() > 1)
            shared.staging.frequency = parseFloat(tokens[1], 220.0f);
        if (tokens.size() > 2)
            shared.staging.modalPartials = parseInt(tokens[2], 8);
        if (tokens.size() > 3)
            shared.staging.modalDecay = parseFloat(tokens[3], 0.7f);
        if (tokens.size() > 4)
            shared.staging.modalInharmonicity = parseFloat(tokens[4], 0.0f);
        shared.paramsReady.store(true, std::memory_order_release);
        std::cout << "Modal: fund=" << shared.staging.frequency
                  << " partials=" << shared.staging.modalPartials
                  << " decay=" << shared.staging.modalDecay << "s"
                  << " inharm=" << shared.staging.modalInharmonicity << std::endl;
        return true;
    }

    if (cmd == "ping")
    {
        // ping <fund> [tone] [damping] [brightness]
        // West Coast LPG voice: carrier (saw/square morph) → LowPassGate.
        // LPG fires on every note-on / sequencer step automatically (see
        // AudioEngine note-on handler).
        shared.staging.source = SourceType::Ping;
        if (tokens.size() > 1)
            shared.staging.frequency = parseFloat(tokens[1], 220.0f);
        if (tokens.size() > 2)
            shared.staging.pingTone = parseFloat(tokens[2], 1.0f);
        if (tokens.size() > 3)
            shared.staging.pingDamping = parseFloat(tokens[3], 0.3f);
        if (tokens.size() > 4)
            shared.staging.pingBrightness = parseFloat(tokens[4], 0.7f);
        shared.paramsReady.store(true, std::memory_order_release);
        std::cout << "Ping (LPG): fund=" << shared.staging.frequency
                  << " tone="       << shared.staging.pingTone
                  << " damping="    << shared.staging.pingDamping
                  << " brightness=" << shared.staging.pingBrightness
                  << std::endl;
        return true;
    }

    if (cmd == "bowed")
    {
        // bowed <fund> [pressure] [position] [damping]
        // Friction-driven bowed-string source.  Pitch tracks `frequency`;
        // pressure / position / damping match the slothrop 3-knob Bow UI.
        // Bow velocity is gated by note-on / note-off in the audio engine
        // (set to bowedVelocity on note-on, back to 0 on note-off).
        shared.staging.source = SourceType::Bowed;
        if (tokens.size() > 1)
            shared.staging.frequency = parseFloat(tokens[1], 220.0f);
        if (tokens.size() > 2)
            shared.staging.bowedPressure = parseFloat(tokens[2], 0.6f);
        if (tokens.size() > 3)
            shared.staging.bowedPosition = parseFloat(tokens[3], 0.1f);
        if (tokens.size() > 4)
            shared.staging.bowedDamping = parseFloat(tokens[4], 0.2f);
        shared.paramsReady.store(true, std::memory_order_release);
        std::cout << "Bowed: fund=" << shared.staging.frequency
                  << " pressure=" << shared.staging.bowedPressure
                  << " position=" << shared.staging.bowedPosition
                  << " damping="  << shared.staging.bowedDamping
                  << std::endl;
        return true;
    }

    if (cmd == "harmonic")
    {
        // harmonic <fund> [low] [mid] [high] [shape] [partials]
        // Plaits-style additive: fund tracks `frequency`, low/mid/high are
        // band amplitudes (LOW=partials 1..3, MID=4..7, HIGH=8..32) in
        // [0, 1], shape is within-band linear taper in [0, 1], partials
        // caps the active partial count in [1, 32] as a CPU knob.
        shared.staging.source = SourceType::Harmonic;
        if (tokens.size() > 1)
            shared.staging.frequency = parseFloat(tokens[1], 220.0f);
        if (tokens.size() > 2)
            shared.staging.harmonicLow = parseFloat(tokens[2], 1.0f);
        if (tokens.size() > 3)
            shared.staging.harmonicMid = parseFloat(tokens[3], 0.5f);
        if (tokens.size() > 4)
            shared.staging.harmonicHigh = parseFloat(tokens[4], 0.25f);
        if (tokens.size() > 5)
            shared.staging.harmonicShape = parseFloat(tokens[5], 0.0f);
        if (tokens.size() > 6)
            shared.staging.harmonicPartials = parseInt(tokens[6],
                ideath::HarmonicOscillator::kMaxPartials);
        shared.paramsReady.store(true, std::memory_order_release);
        std::cout << "Harmonic: fund=" << shared.staging.frequency
                  << " low="    << shared.staging.harmonicLow
                  << " mid="    << shared.staging.harmonicMid
                  << " high="   << shared.staging.harmonicHigh
                  << " shape="  << shared.staging.harmonicShape
                  << " partials=" << shared.staging.harmonicPartials
                  << std::endl;
        return true;
    }

    if (cmd == "fold")
    {
        if (tokens.size() > 1 && tokens[1] == "off")
        {
            shared.staging.foldEnabled = false;
            shared.paramsReady.store(true, std::memory_order_release);
            std::cout << "Wavefolder OFF" << std::endl;
            return true;
        }

        shared.staging.foldEnabled = true;
        if (tokens.size() > 1)
            shared.staging.foldDrive = parseFloat(tokens[1], 1.0f);
        if (tokens.size() > 2)
            shared.staging.foldMix = parseFloat(tokens[2], 1.0f);
        shared.paramsReady.store(true, std::memory_order_release);
        std::cout << "Wavefolder: drive=" << shared.staging.foldDrive
                  << " mix=" << shared.staging.foldMix << std::endl;
        return true;
    }

    if (cmd == "loop")
    {
        if (tokens.size() < 2)
        {
            std::cout << "Usage: loop <rec|stop|play|overdub|off> [feedback] [mix]" << std::endl;
            return true;
        }

        if (tokens[1] == "rec")
            shared.staging.loopAction = VoiceParams::LoopAction::Record;
        else if (tokens[1] == "stop")
            shared.staging.loopAction = VoiceParams::LoopAction::Stop;
        else if (tokens[1] == "play")
            shared.staging.loopAction = VoiceParams::LoopAction::Play;
        else if (tokens[1] == "overdub" || tokens[1] == "dub")
            shared.staging.loopAction = VoiceParams::LoopAction::Overdub;
        else if (tokens[1] == "off")
            shared.staging.loopAction = VoiceParams::LoopAction::Off;
        else if (tokens[1] == "feedback" && tokens.size() > 2)
        {
            shared.staging.loopFeedback = parseFloat(tokens[2], 0.8f);
            shared.staging.loopAction = VoiceParams::LoopAction::None;
        }
        else if (tokens[1] == "mix" && tokens.size() > 2)
        {
            shared.staging.loopMix = parseFloat(tokens[2], 1.0f);
            shared.staging.loopAction = VoiceParams::LoopAction::None;
        }

        shared.paramsReady.store(true, std::memory_order_release);
        std::cout << "Loop: " << tokens[1] << std::endl;
        return true;
    }

    if (cmd == "stop")
    {
        shared.staging.source = SourceType::None;
        shared.paramsReady.store(true, std::memory_order_release);
        shared.stopRequested.store(true, std::memory_order_release);
        // Also stop sequencer
        shared.seqStaging.running = false;
        shared.seqReady.store(true, std::memory_order_release);
        return true;
    }

    std::cout << "Unknown command: " << cmd << ". Type 'help' for commands." << std::endl;
    return true;
}

}} // namespace ideath::repl
