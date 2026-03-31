#pragma once

#include "SharedState.h"
#include <string>

namespace ideath { namespace repl {

/// Parse a command line and update the shared state accordingly.
/// Returns false if the command is "quit" or "exit".
bool parseCommand(const std::string& line, SharedState& shared);

/// Print available commands.
void printHelp();

}} // namespace ideath::repl
