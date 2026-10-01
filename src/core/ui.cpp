#include "core/ui.h"

#include <imgui.h>

#include <algorithm>

#include "core/camera.h"
#include "core/input.h"

namespace glint::ui {

void init() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
  ImGui::StyleColorsDark();
}

void shutdown() { ImGui::DestroyContext(); }

void newFrame(Input& input) {
  ImGui::NewFrame();
  const ImGuiIO& io = ImGui::GetIO();
  input.uiWantsMouse = io.WantCaptureMouse;
  input.uiWantsKeyboard = io.WantCaptureKeyboard;
}

std::optional<std::string> techniqueCombo(const std::vector<std::string>& names, const std::string& current) {
  std::optional<std::string> picked;
  if (ImGui::BeginCombo("Technique", current.empty() ? "<none>" : current.c_str())) {
    for (const std::string& name : names) {
      const bool selected = name == current;
      if (ImGui::Selectable(name.c_str(), selected) && !selected) picked = name;
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  return picked;
}

void FrameStats::add(float frameSeconds, float cpuMs, float gpuMs) {
  m_frame[size_t(m_next)] = frameSeconds * 1000.0f;
  m_cpu[size_t(m_next)] = cpuMs;
  m_gpu[size_t(m_next)] = gpuMs;
  m_next = (m_next + 1) % int(m_frame.size());
  m_count = std::min(m_count + 1, int(m_frame.size()));
}

float FrameStats::average(const std::array<float, 120>& values) const {
  float sum = 0.0f;
  int n = 0;
  for (int i = 0; i < m_count; ++i) {
    if (values[size_t(i)] < 0.0f) continue;  // "not available" samples
    sum += values[size_t(i)];
    ++n;
  }
  return n > 0 ? sum / float(n) : -1.0f;
}

void statsSection(const FrameStats& stats, const GpuStats& gpu, const char* api, const std::string& device,
                  int fbWidth, int fbHeight) {
  if (!ImGui::CollapsingHeader("Stats", ImGuiTreeNodeFlags_DefaultOpen)) return;
  ImGui::Text("%s on %s", api, device.c_str());
  const float ms = stats.averageMs();
  ImGui::Text("%.2f ms/frame (%.0f FPS)", ms, ms > 0.0f ? 1000.0f / ms : 0.0f);
  ImGui::PlotLines("##frametimes", stats.historyMs(), stats.historySize(), stats.historyOffset(), "frame ms", 0.0f,
                   std::max(33.3f, ms * 2.0f), ImVec2(-1.0f, 40.0f));
  const float gpuMs = stats.averageGpuMs();
  if (gpuMs >= 0.0f) {
    ImGui::Text("CPU %.2f ms   GPU %.3f ms (technique)", stats.averageCpuMs(), gpuMs);
  } else {
    ImGui::Text("CPU %.2f ms   GPU -", stats.averageCpuMs());
  }
  if (gpu.valid && gpu.hasPipelineStatistics) {
    ImGui::Text("%llu vertices, %llu triangles", (unsigned long long)gpu.vertices, (unsigned long long)gpu.primitives);
    ImGui::Text("VS %llu, FS %llu invocations", (unsigned long long)gpu.vertexInvocations,
                (unsigned long long)gpu.fragmentInvocations);
  }
  ImGui::Text("framebuffer %d x %d", fbWidth, fbHeight);
}

void cameraSection(Camera& camera) {
  if (!ImGui::CollapsingHeader("Camera")) return;
  int mode = int(camera.mode);
  ImGui::RadioButton("Arcball", &mode, int(Camera::Mode::Arcball));
  ImGui::SameLine();
  ImGui::RadioButton("Fly", &mode, int(Camera::Mode::Fly));
  camera.mode = Camera::Mode(mode);
  ImGui::TextDisabled(camera.mode == Camera::Mode::Arcball ? "RMB rotate, MMB pan, wheel zoom"
                                                           : "RMB look, WASD move, Q/E down/up, Shift fast");

  ImGui::SliderFloat("rotate speed", &camera.rotateSpeed, 0.1f, 3.0f);
  if (camera.mode == Camera::Mode::Fly) ImGui::SliderFloat("move speed", &camera.moveSpeed, 0.5f, 20.0f);
  ImGui::SliderFloat("fov y", &camera.fovY, 10.0f, 120.0f, "%.0f deg");
  ImGui::DragFloatRange2("near / far", &camera.nearZ, &camera.farZ, 0.05f, 0.01f, 1000.0f, "%.2f");
  camera.nearZ = std::max(camera.nearZ, 0.001f);
  camera.farZ = std::max(camera.farZ, camera.nearZ + 0.01f);
  ImGui::Text("eye    (%.2f, %.2f, %.2f)", camera.eye.x, camera.eye.y, camera.eye.z);
  ImGui::Text("target (%.2f, %.2f, %.2f)", camera.target.x, camera.target.y, camera.target.z);
  if (ImGui::Button("Reset view")) camera.resetView();
}

}  // namespace glint::ui
