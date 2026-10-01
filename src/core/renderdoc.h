#pragma once

// RenderDoc's in-application API (external/renderdoc/renderdoc_app.h): capture a frame from inside the app
// and open it in the RenderDoc UI. API-agnostic — RenderDoc captures whatever API the process uses.
//
// The library must be loaded *before* the graphics API is initialised (before the GL context or VK instance
// exists), so it can hook it: call load() first thing, when --renderdoc is given. If the app was launched
// from qrenderdoc, the library is already injected and load() just picks it up.

#include <string>

namespace glint::renderdoc {

// RenderDoc install directory (contains lib/librenderdoc.so). From $RENDERDOC_DIR, else the build-time
// default (GLINT_RENDERDOC_DIR, ~/tools/renderdoc).
std::string installDir();

// Linux, GL only: RenderDoc hooks GL by *symbol interposition* (its glX* exports must come before libGL's),
// which only works if librenderdoc is preloaded — a dlopen at runtime is too late. So the GL app re-executes
// itself once with LD_PRELOAD set. Returns only if no re-exec was needed (already preloaded/injected).
// (VK needs none of this: RenderDoc hooks it through a loader layer.)
void preloadByReexec(char** argv);

// Finds an injected librenderdoc or loads it from installDir(). Captures go to <repo>/captures/<app>_*.rdc.
bool load(const std::string& app);
bool available();

// Capture the next frame presented (same as pressing F12 / PrtScn in the app).
void triggerCapture();

// "RenderDoc" collapsing header: capture button, captures so far, open the latest in qrenderdoc.
void uiSection();

}  // namespace glint::renderdoc
