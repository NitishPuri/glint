// glint_gl — Phase 1 skeleton: proves the GL-side dependencies compile and link.
// The real app (window, glad load, KHR_debug, ImGui, technique registry) arrives in Phase 3.

// glad provides all GL declarations. (glint_core defines GLFW_INCLUDE_NONE, so <GLFW/glfw3.h> never pulls in
// the system <GL/gl.h>, which only declares GL 1.x on Linux anyway.)
#include <glad/gl.h>
//
#include <GLFW/glfw3.h>
#include <fmt/core.h>
#include <imgui.h>

int main() {
  // GL: every entry point past 1.1 is a function pointer fetched at runtime; nothing is loaded yet
  // because there's no context. (VK has the same idea, but the loader library does it for you.)
  auto loader = &gladLoadGL;
  fmt::print("glint_gl skeleton\n  GLFW  {}\n  ImGui {}\n  glad  {} (GL 4.6 core + KHR_debug, loader {})\n",
             glfwGetVersionString(), IMGUI_VERSION, GLAD_GENERATOR_VERSION, loader ? "linked" : "missing");
  fmt::print("  assets  {}\n  shaders {}\n", GLINT_ASSET_DIR, GLINT_SHADER_DIR);
  return 0;
}
