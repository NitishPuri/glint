#pragma once

// What this binary was built from, for the run-log header (so logs from different runs can be compared).

namespace glint::build {

extern const char* const kGitCommit;  // short hash, or "unknown" outside a git checkout
extern const bool kGitDirty;          // tracked files had uncommitted changes at build time

#ifdef NDEBUG
inline constexpr const char* kBuildType = "Release";
#else
inline constexpr const char* kBuildType = "Debug";
#endif

}  // namespace glint::build
