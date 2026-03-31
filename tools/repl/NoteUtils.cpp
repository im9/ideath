#include "NoteUtils.h"
#include <cmath>
#include <cctype>

namespace ideath { namespace repl {

float noteToFreq(const std::string& note)
{
    if (note.empty()) return 0.0f;

    // Parse letter
    int semitone = -1;
    switch (std::toupper(note[0]))
    {
        case 'C': semitone = 0;  break;
        case 'D': semitone = 2;  break;
        case 'E': semitone = 4;  break;
        case 'F': semitone = 5;  break;
        case 'G': semitone = 7;  break;
        case 'A': semitone = 9;  break;
        case 'B': semitone = 11; break;
        default: return 0.0f;
    }

    size_t pos = 1;

    // Parse optional sharp/flat
    if (pos < note.size())
    {
        if (note[pos] == '#') { ++semitone; ++pos; }
        else if (note[pos] == 'b') { --semitone; ++pos; }
    }

    // Parse octave
    if (pos >= note.size() || !std::isdigit(note[pos]))
        return 0.0f;

    int octave = note[pos] - '0';

    // MIDI note number
    int midi = (octave + 1) * 12 + semitone;

    // Frequency
    return 440.0f * std::pow(2.0f, static_cast<float>(midi - 69) / 12.0f);
}

}} // namespace ideath::repl
