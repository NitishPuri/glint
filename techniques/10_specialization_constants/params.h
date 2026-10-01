#pragma once

// 10_specialization_constants — shared by gl.cpp and vk.cpp. One "uber" fragment shader with three
// lighting models (Phong, toon, textured; from Sascha Willems' specializationconstants sample, whose
// shaders Glint_vk carried as uber.vert/frag). Each model becomes its own pipeline/program, drawn side by
// side in three viewports.
//   VK: layout(constant_id = N) in SPIR-V, values supplied at pipeline creation (VkSpecializationInfo).
//   GL: #defines injected into the source before compiling.

#include <imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace glint::uber {

inline constexpr int kModels = 3;
inline constexpr const char* kModelNames[kModels] = {"Phong", "Toon", "Textured"};
inline const glm::vec3 kCameraHome{0.0f, 0.0f, 4.0f};

struct Params {
  glm::vec3 color{1.0f, 0.2f, 0.2f};
  glm::vec3 lightPosition{3.0f, 3.0f, 3.0f};
  float toonDesaturation = 0.5f;  // a *constant* in the shader: changing it rebuilds the toon pipeline/program
  float rotationSpeed = 20.0f;
};

// Returns true if toonDesaturation changed (-> rebuild).
inline bool paramsUi(Params& p) {
  ImGui::ColorEdit3("color", &p.color.x);
  ImGui::SliderFloat3("light position", &p.lightPosition.x, -10.0f, 10.0f);
  ImGui::SliderFloat("rotation", &p.rotationSpeed, -90.0f, 90.0f, "%.0f deg/s");
  const float old = p.toonDesaturation;
  ImGui::SliderFloat("toon desaturation", &p.toonDesaturation, 0.0f, 1.0f);
  ImGui::TextDisabled("(a specialization constant / #define: changing it rebuilds that pipeline)");
  return p.toonDesaturation != old;
}

// std140 block, same in both APIs.
struct Uniforms {
  glm::mat4 projection;
  glm::mat4 modelView;
  glm::vec4 lightPosition;  // view space
  glm::vec4 color;
};

inline Uniforms makeUniforms(const glm::mat4& projection, const glm::mat4& view, float angleDegrees,
                             const Params& p) {
  const glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(angleDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
  return {projection, view * model, view * glm::vec4(p.lightPosition, 1.0f), glm::vec4(p.color, 1.0f)};
}

// Model name over each third of the window (ImGui's foreground draw list, so identical in both apps).
inline void drawLabels(glm::ivec2 framebufferSize) {
  ImDrawList* draw = ImGui::GetForegroundDrawList();
  const float third = float(framebufferSize.x) / kModels;
  for (int i = 0; i < kModels; ++i) {
    draw->AddText(ImVec2(third * float(i) + 10.0f, float(framebufferSize.y) - 30.0f), IM_COL32(255, 255, 255, 220),
                  kModelNames[i]);
  }
}

}  // namespace glint::uber
