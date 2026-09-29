#pragma once

// Per-run log file + header, for comparing runs later (regressions, driver changes, "what changed?").
//
//   logs/glint_vk_2026-09-30_14-02-11.log   (repo-local, gitignored)
//
// The header records what the numbers in the rest of the file depend on: commit (and whether the tree
// was dirty), build type, compiler, command line and config. Each app then adds its GPU/driver lines,
// and a FrameTimeSummary per technique.

#include <string_view>

#include "core/config.h"

namespace glint {

// Opens the log file (config.logFile, or a new timestamped one unless --no-log-file) and writes the header.
void startRunLog(std::string_view app, const Config& config, int argc, const char* const* argv);

}  // namespace glint
