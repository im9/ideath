#include "CommandParser.h"
#include "NoteUtils.h"
#include <sstream>
#include <vector>
#include <iostream>
#include <cctype>

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
  wt <square|saw|tri|sine> <freq>  Wavetable source
  noise                         Noise source
  filter <lp|hp|bp> <freq> <Q>  Biquad filter (or "filter off")
  crush <bits> <rate>           BitCrusher (or "crush off")
  sat <drive>                   Saturation (or "sat off")
  delay <time> <feedback>       Delay line (or "delay off")
  lfo <sine|tri|square|saw|sh> <rate> <pitch|filter|vol> <depth>
                                LFO modulation (or "lfo off")
  env <A> <D> <S> <R>          Set ADSR envelope (or "env off")
  note <C4|freq>               Trigger note (with envelope if set)
  release                      Release note
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

    if (cmd == "noise")
    {
        shared.staging.source = SourceType::Noise;
        shared.paramsReady.store(true, std::memory_order_release);
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

    if (cmd == "stop")
    {
        shared.staging.source = SourceType::None;
        shared.paramsReady.store(true, std::memory_order_release);
        shared.stopRequested.store(true, std::memory_order_release);
        return true;
    }

    std::cout << "Unknown command: " << cmd << ". Type 'help' for commands." << std::endl;
    return true;
}

}} // namespace ideath::repl
