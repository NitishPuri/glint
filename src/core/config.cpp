#include "core/config.h"

#include <fmt/format.h>

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string_view>

namespace glint {

Config parseArgs(int argc, const char* const* argv) {
  Config config;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    auto value = [&]() -> std::string {
      if (i + 1 >= argc) throw std::invalid_argument(fmt::format("{} needs a value", arg));
      return argv[++i];
    };

    if (arg == "-h" || arg == "--help") {
      config.showHelp = true;
    } else if (arg == "--size") {
      const std::string size = value();
      if (std::sscanf(size.c_str(), "%dx%d", &config.width, &config.height) != 2 || config.width <= 0 ||
          config.height <= 0) {
        throw std::invalid_argument(fmt::format("--size expects WxH, got '{}'", size));
      }
    } else if (arg == "--no-vsync") {
      config.vsync = false;
    } else if (arg == "--validation") {
      config.validation = true;
    } else if (arg == "--no-validation") {
      config.validation = false;
    } else if (arg == "--log") {
      config.logFile = value();
    } else if (arg == "--frames") {
      config.exitAfterFrames = std::atoi(value().c_str());
      if (config.exitAfterFrames <= 0) throw std::invalid_argument("--frames expects a positive number");
    } else if (arg == "--screenshot") {
      config.screenshot = value();
      if (config.exitAfterFrames == 0) config.exitAfterFrames = 60;
    } else if (arg == "--assets") {
      config.assetDir = value();
    } else if (!arg.empty() && arg[0] == '-') {
      throw std::invalid_argument(fmt::format("unknown option '{}'", arg));
    } else if (config.technique.empty()) {
      config.technique = arg;
    } else {
      throw std::invalid_argument(fmt::format("unexpected argument '{}'", arg));
    }
  }
  return config;
}

std::string usage(const char* exe) {
  return fmt::format(
      "usage: {} [technique] [options]\n"
      "  technique          e.g. 01_triangle (default: the first one)\n"
      "  --size WxH         window size (default 1600x900)\n"
      "  --no-vsync         uncapped frame rate\n"
      "  --validation       VK validation layers / GL debug context (default in Debug builds)\n"
      "  --no-validation\n"
      "  --log FILE         also write the log to FILE\n"
      "  --assets DIR       asset directory (default: the repo's assets/)\n"
      "  --frames N         quit after N frames\n"
      "  --screenshot FILE  save the last frame to FILE (PNG) and quit (after 60 frames unless --frames)\n"
      "  -h, --help\n",
      exe);
}

}  // namespace glint
