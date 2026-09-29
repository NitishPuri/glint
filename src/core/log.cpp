#include "core/log.h"

#include <chrono>
#include <cstdio>
#include <mutex>

namespace glint::log {

namespace {

std::mutex g_mutex;
std::FILE* g_file = nullptr;
#ifdef NDEBUG
Level g_minLevel = Level::Info;
#else
Level g_minLevel = Level::Debug;
#endif

const char* levelTag(Level level) {
  switch (level) {
    case Level::Debug: return "debug";
    case Level::Info: return "info ";
    case Level::Warn: return "WARN ";
    case Level::Error: return "ERROR";
  }
  return "?";
}

// Time since the first log line: short, and handy for spotting slow startup steps.
double secondsSinceStart() {
  static const auto start = std::chrono::steady_clock::now();
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

void write(Level level, std::string_view message) {
  if (level < g_minLevel) return;
  std::lock_guard lock(g_mutex);
  const std::string line = fmt::format("[{:8.3f}] {} {}\n", secondsSinceStart(), levelTag(level), message);
  std::FILE* console = level >= Level::Warn ? stderr : stdout;
  std::fputs(line.c_str(), console);
  if (g_file) {
    std::fputs(line.c_str(), g_file);
    std::fflush(g_file);  // keep the file useful even if we crash right after
  }
}

void setFile(const std::string& path) {
  std::lock_guard lock(g_mutex);
  if (g_file) std::fclose(g_file);
  g_file = path.empty() ? nullptr : std::fopen(path.c_str(), "w");
  if (!path.empty() && !g_file) fmt::print(stderr, "log: cannot open '{}'\n", path);
}

void setMinLevel(Level level) { g_minLevel = level; }

}  // namespace glint::log
