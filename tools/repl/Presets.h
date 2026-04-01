#pragma once

#include "SharedState.h"
#include <string>
#include <vector>

namespace ideath { namespace repl {

struct PresetEntry {
    const char* name;
    const char* description;
    VoiceParams params;
};

/// Get a preset by name. Returns nullptr if not found.
const PresetEntry* getPreset(const std::string& name);

/// Get list of all available presets.
const std::vector<PresetEntry>& getAllPresets();

}} // namespace ideath::repl
