#pragma once

#include <string>

namespace ideath { namespace repl {

/// Parse a note name like "C4", "C#4", "Bb3" to frequency in Hz.
/// Returns 0.0f if parsing fails.
float noteToFreq(const std::string& note);

}} // namespace ideath::repl
