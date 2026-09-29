#include "core/run_log.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <string>

#include "core/build_info.h"
#include "core/log.h"

namespace glint {

namespace {

std::tm localNow() {
  const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &t);
#else
  localtime_r(&t, &local);
#endif
  return local;
}

std::string compilerName() {
#if defined(__clang__)
  return fmt::format("clang {}.{}.{}", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
  return fmt::format("gcc {}.{}.{}", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
  return fmt::format("msvc {}", _MSC_VER);
#else
  return "unknown";
#endif
}

}  // namespace

void startRunLog(std::string_view app, const Config& config, int argc, const char* const* argv) {
  const std::tm now = localNow();
  if (config.writeLogFile) {
    std::string path = config.logFile;
    if (path.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(GLINT_LOG_DIR, ec);
      path = fmt::format("{}/{}_{:04}-{:02}-{:02}_{:02}-{:02}-{:02}.log", GLINT_LOG_DIR, app, now.tm_year + 1900,
                         now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec);
    }
    if (log::setFile(path)) log::info("log file {}", path);
  }

  std::string commandLine;
  for (int i = 0; i < argc; ++i) commandLine += (i ? " " : "") + std::string(argv[i]);

  log::info("run      {} {:04}-{:02}-{:02} {:02}:{:02}:{:02}", app, now.tm_year + 1900, now.tm_mon + 1, now.tm_mday,
            now.tm_hour, now.tm_min, now.tm_sec);
  log::info("build    {}{} {} {}", build::kGitCommit, build::kGitDirty ? " (dirty)" : "", build::kBuildType,
            compilerName());
  log::info("args     {}", commandLine);
  log::info("config   technique={} size={}x{} vsync={} validation={}",
            config.technique.empty() ? "<first>" : config.technique, config.width, config.height, config.vsync,
            config.validation);
}

}  // namespace glint
