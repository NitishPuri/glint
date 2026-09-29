#pragma once

// Startup settings from the command line, shared by both apps:
//   glint_vk [technique] [--size WxH] [--no-vsync] [--validation|--no-validation] [--log FILE] [--assets DIR]
//            [--frames N] [--screenshot FILE]

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
  std::string logFile;   // empty: console only
  std::string assetDir;  // empty: GLINT_ASSET_DIR
  int exitAfterFrames = 0;  // > 0: quit after this many frames (for scripted runs)
  std::string screenshot;   // non-empty: save the last frame as PNG before quitting
  bool showHelp = false;
};

// Throws std::invalid_argument with a readable message on bad arguments.
Config parseArgs(int argc, const char* const* argv);

std::string usage(const char* exe);

}  // namespace glint
