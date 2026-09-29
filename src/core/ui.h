#pragma once

// ImGui pieces shared by both apps: context setup and the common panels.
// The platform/renderer backends (imgui_impl_glfw + the OpenGL3 / Vulkan renderer backend) are set up by
// each app, because initialising and rendering them is exactly where GL and VK differ.
//
// Per frame, in each app:
//   ImGui_ImplOpenGL3_NewFrame() / ImGui_ImplVulkan_NewFrame();
//   ImGui_ImplGlfw_NewFrame();
//   ui::newFrame(window.input());
//   ... ImGui::Begin("glint"); ui::techniqueCombo(...); ui::statsSection(...); ... ImGui::End();
//   ImGui::Render();  then the backend draws ImGui::GetDrawData()

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace glint {

class Camera;
class Input;

namespace ui {

// ImGui::CreateContext + config flags + style. Call before initialising the ImGui backends.
void init();
void shutdown();

// ImGui::NewFrame(), then tells Input whether ImGui wants the mouse/keyboard (so the camera ignores
// drags that start on a panel).
void newFrame(Input& input);

// Combo box listing the techniques. Returns the name the user picked, if it differs from `current`.
std::optional<std::string> techniqueCombo(const std::vector<std::string>& names, const std::string& current);

// Frame-time history for the stats section.
class FrameStats {
 public:
  void add(float dtSeconds);
  float averageMs() const;
  const float* historyMs() const { return m_history.data(); }
  int historySize() const { return int(m_history.size()); }
  int historyOffset() const { return m_next; }

 private:
  std::array<float, 120> m_history{};
  int m_next = 0;
  int m_count = 0;
};

// "Stats" collapsing header: API + device name, frame time plot, framebuffer size.
void statsSection(const FrameStats& stats, const char* api, const std::string& device, int fbWidth, int fbHeight);

// "Camera" collapsing header: mode, speeds, fov/near/far, eye/target, reset.
void cameraSection(Camera& camera);

}  // namespace ui
}  // namespace glint
