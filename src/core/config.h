#pragma once

// Startup settings from the command line, shared by both apps:
//   glint_vk [technique] [--size WxH] [--no-vsync] [--validation|--no-validation] [--log FILE] [--assets DIR]
//            [--frames N] [--screenshot FILE] [--fixed-dt S] [--no-log-file] [--renderdoc]

#include <string>

namespace glint {

struct Config {
  std::string technique;  // empty: the first registered one
  int width = 1600;
  int height = 900;
  bool vsync = true;
  // VK validation layers / GL debug context + KHR_debug callback.
#ifdef NDEBUG
  bool validation = false;
#else
  bool validation = true;
#endif
  std::string logFile;       // empty: logs/<app>_<date>_<time>.log (see core/run_log.h)
  bool writeLogFile = true;  // --no-log-file: console only
  std::string assetDir;  // empty: GLINT_ASSET_DIR
  int exitAfterFrames = 0;  // > 0: quit after this many frames (for scripted runs)
  std::string screenshot;   // non-empty: save the last frame as PNG before quitting (camera input is ignored)
  float fixedDt = 0.0f;     // > 0: every frame advances the simulation by exactly this many seconds
  bool renderdoc = false;   // load RenderDoc's in-app API (before the window/context/instance exist)
  bool showHelp = false;
};

// Throws std::invalid_argument with a readable message on bad arguments.
Config parseArgs(int argc, const char* const* argv);

std::string usage(const char* exe);

}  // namespace glint
