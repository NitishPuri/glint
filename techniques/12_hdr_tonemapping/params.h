#pragma once

// 12_hdr_tonemapping — shared by gl.cpp and vk.cpp. A room lit by four point lights whose intensities span
// 25x (4 -> 100). Lighting is computed in *linear* space into a float (RGBA16F) target, so nothing clips;
// a second pass applies exposure, a tone-mapping operator and the sRGB encode the display expects.
// "Gamma-space" mode reproduces what 01-11 do (texture bytes used as if linear, no encode) for comparison.
// Read: GPU Gems 3 ch24 "The Importance of Being Linear".

#include <imgui.h>

#include <array>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace glint::hdr {

inline const glm::vec3 kCameraHome{5.0f, 4.0f, -13.0f};  // 08's view of the same room
inline constexpr int kLights = 4;
inline constexpr const char* kOperators[] = {"none (clamp)", "Reinhard", "ACES (fitted)"};

struct Light {
  glm::vec3 position;
  glm::vec3 color;
  float intensity;  // radiant intensity; falls off with 1/d^2
};

struct Params {
  std::array<Light, kLights> lights{{
      {{-4.0f, 2.0f, 0.0f}, {1.0f, 0.75f, 0.45f}, 4.0f},
      {{3.0f, 2.0f, 3.0f}, {0.5f, 0.7f, 1.0f}, 15.0f},
      {{0.0f, 3.0f, -4.0f}, {1.0f, 1.0f, 1.0f}, 40.0f},
      {{6.0f, 1.5f, -2.0f}, {1.0f, 0.25f, 0.2f}, 100.0f},
  }};
  float exposureEv = 0.0f;  // exposure in stops: scale = 2^ev
  int tonemap = 2;          // index into kOperators
  bool linearWorkflow = true;
};

inline void paramsUi(Params& p) {
  ImGui::SliderFloat("exposure", &p.exposureEv, -6.0f, 6.0f, "%+.1f EV");
  ImGui::Combo("tone mapping", &p.tonemap, kOperators, IM_ARRAYSIZE(kOperators));
  ImGui::Checkbox("linear workflow (off: the gamma-space way of 01-11)", &p.linearWorkflow);
  for (int i = 0; i < kLights; ++i) {
    ImGui::PushID(i);
    ImGui::Text("light %d", i);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("##intensity", &p.lights[size_t(i)].intensity, 0.0f, 200.0f, "%.0f");
    ImGui::SameLine();
    ImGui::ColorEdit3("##color", &p.lights[size_t(i)].color.x, ImGuiColorEditFlags_NoInputs);
    ImGui::PopID();
  }
}

// Scene pass uniform block (std140; vec4 arrays have a 16-byte stride in std140 too).
struct SceneUniforms {
  glm::mat4 viewProjection;
  glm::vec4 cameraPosition;
  std::array<glm::vec4, kLights> lightPositions;  // xyz
  std::array<glm::vec4, kLights> lightColors;     // rgb * intensity
  glm::vec4 settings;                             // x = 1: linear workflow
};

// Per draw (push constants in VK, the binding-15 block in GL): model matrix + emissive color (a > 0: emissive).
struct DrawConstants {
  glm::mat4 model;
  glm::vec4 emissive;
};

// Tone-mapping pass push constants.
struct TonemapConstants {
  glm::vec4 params;  // x = exposure scale, y = operator, z = 1: encode to sRGB
};

inline SceneUniforms makeSceneUniforms(const glm::mat4& viewProjection, const glm::vec3& eye, const Params& p) {
  SceneUniforms u{viewProjection, glm::vec4(eye, 1.0f), {}, {}, glm::vec4(p.linearWorkflow ? 1.0f : 0.0f)};
  for (int i = 0; i < kLights; ++i) {
    const Light& l = p.lights[size_t(i)];
    u.lightPositions[size_t(i)] = glm::vec4(l.position, 1.0f);
    u.lightColors[size_t(i)] = glm::vec4(l.color * l.intensity, 0.0f);
  }
  return u;
}

// The small cube that shows light i. Its emissive color is the light's (HDR) color, so bright lights saturate.
inline DrawConstants lightMarker(const Params& p, int i) {
  const Light& l = p.lights[size_t(i)];
  return {glm::scale(glm::translate(glm::mat4(1.0f), l.position), glm::vec3(0.12f)),
          glm::vec4(l.color * l.intensity * 0.25f, 1.0f)};
}

inline TonemapConstants makeTonemapConstants(const Params& p) {
  return {glm::vec4(std::exp2(p.exposureEv), float(p.tonemap), p.linearWorkflow ? 1.0f : 0.0f, 0.0f)};
}

}  // namespace glint::hdr
