#include "core/renderdoc.h"

#include <imgui.h>
#include <renderdoc_app.h>

#include <cstdlib>
#include <filesystem>

#include "core/log.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace glint::renderdoc {

namespace {
RENDERDOC_API_1_6_0* g_api = nullptr;
}

std::string installDir() {
  if (const char* dir = std::getenv("RENDERDOC_DIR")) return dir;
  return GLINT_RENDERDOC_DIR;
}

void preloadByReexec(char** argv) {
#ifndef _WIN32
  if (dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD)) return;  // preloaded or injected by qrenderdoc
  if (std::getenv("GLINT_RENDERDOC_REEXEC")) return;               // tried once already: don't loop
  const std::string library = installDir() + "/lib/librenderdoc.so";
  setenv("LD_PRELOAD", library.c_str(), 1);
  setenv("GLINT_RENDERDOC_REEXEC", "1", 1);
  fmt::print("RenderDoc: restarting with LD_PRELOAD={}\n", library);
  execv("/proc/self/exe", argv);  // only returns on failure
  fmt::print(stderr, "RenderDoc: re-exec failed; GL frames can't be captured\n");
#else
  (void)argv;  // Windows: in-app LoadLibrary works for GL too
#endif
}

bool load(const std::string& app) {
  pRENDERDOC_GetAPI getApi = nullptr;
#ifdef _WIN32
  HMODULE module = GetModuleHandleA("renderdoc.dll");  // injected by qrenderdoc?
  if (!module) module = LoadLibraryA((installDir() + "/renderdoc.dll").c_str());
  if (module) getApi = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"));
#else
  // RTLD_NOLOAD: only succeeds if qrenderdoc already injected it into this process.
  void* module = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
  if (!module) module = dlopen((installDir() + "/lib/librenderdoc.so").c_str(), RTLD_NOW);
  if (module) getApi = reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(module, "RENDERDOC_GetAPI"));
#endif
  if (!getApi || getApi(eRENDERDOC_API_Version_1_6_0, reinterpret_cast<void**>(&g_api)) != 1) {
    log::warn("RenderDoc: could not load {}/lib/librenderdoc.so (set RENDERDOC_DIR?)", installDir());
    g_api = nullptr;
    return false;
  }
  int major = 0, minor = 0, patch = 0;
  g_api->GetAPIVersion(&major, &minor, &patch);
  const std::string captures = std::string(GLINT_CAPTURE_DIR) + "/" + app;
  std::error_code ec;
  std::filesystem::create_directories(GLINT_CAPTURE_DIR, ec);
  g_api->SetCaptureFilePathTemplate(captures.c_str());
  log::info("RenderDoc {}.{}.{} loaded; F12 or the panel button captures, files go to {}_*.rdc", major, minor,
            patch, captures);
  return true;
}

bool available() { return g_api != nullptr; }

void triggerCapture() {
  if (g_api) g_api->TriggerCapture();
}

void uiSection() {
  if (!ImGui::CollapsingHeader("RenderDoc")) return;
  if (!g_api) {
    ImGui::TextDisabled("not loaded: start with --renderdoc");
    return;
  }
  if (ImGui::Button("Capture next frame")) triggerCapture();
  const uint32_t count = g_api->GetNumCaptures();
  ImGui::SameLine();
  ImGui::Text("%u captured", count);
  if (count == 0) return;

  char path[1024] = {};
  uint32_t length = sizeof(path);
  g_api->GetCapture(count - 1, path, &length, nullptr);
  ImGui::TextWrapped("%s", path);
  if (ImGui::Button("Open latest in RenderDoc")) {
    // Starts qrenderdoc on that capture and connects it to this process (for further captures).
    g_api->LaunchReplayUI(1, path);
  }
}

}  // namespace glint::renderdoc
