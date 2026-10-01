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

#include "core/gpu_stats.h"

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

// Rolling history (last 120 frames) of frame time, CPU time and GPU time, for the stats section.
//   frame: wall-clock time between frames (vsync-bound when vsync is on)
//   cpu:   time the app spent producing the frame, excluding waits for vsync / the GPU
//   gpu:   GPU time of the technique's commands (from timer queries; may lag a few frames)
class FrameStats {
 public:
  void add(float frameSeconds, float cpuMs, float gpuMs);  // gpuMs < 0: not available
  float averageMs() const { return average(m_frame); }
  float averageCpuMs() const { return average(m_cpu); }
  float averageGpuMs() const { return average(m_gpu); }
  const float* historyMs() const { return m_frame.data(); }
  int historySize() const { return int(m_frame.size()); }
  int historyOffset() const { return m_next; }

 private:
  float average(const std::array<float, 120>& values) const;
  std::array<float, 120> m_frame{}, m_cpu{}, m_gpu{};
  int m_next = 0;
  int m_count = 0;
};

// "Stats" collapsing header: API + device, frame/CPU/GPU time, pipeline statistics, framebuffer size.
void statsSection(const FrameStats& stats, const GpuStats& gpu, const char* api, const std::string& device,
                  int fbWidth, int fbHeight);

// "Camera" collapsing header: mode, speeds, fov/near/far, eye/target, reset.
void cameraSection(Camera& camera);

}  // namespace ui
}  // namespace glint
