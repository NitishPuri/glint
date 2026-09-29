#pragma once

// 08_shadow_mapping — shared by gl.cpp and vk.cpp.

#include <imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "core/camera.h"

namespace glint::shadow_mapping {

inline constexpr int kShadowMapSizes[] = {512, 1024, 2048, 4096};

// Where the camera starts (Glint_gl's per-scene position).
inline const glm::vec3 kCameraHome = glm::vec3(5.0f, 4.0f, -13.0f);

struct Params {
  glm::vec3 lightPosition{4.0f, 4.0f, 4.0f};  // directional: the light shines from here towards the origin
  glm::vec3 lightColor{1.0f};
  float lightPower = 2.0f;  // no distance falloff for a directional light
  float ambient = 0.1f;
  float specular = 0.3f;
  bool spotlight = false;  // perspective light frustum instead of orthographic
  bool pcf = true;         // 4 Poisson taps with hardware 2x2 compare each, vs a single tap
  float bias = 0.005f;     // depth bias against shadow acne
  int shadowMapSize = 2;   // index into kShadowMapSizes
  bool showShadowMap = false;
};

// Returns true if the shadow map must be recreated (size changed).
inline bool paramsUi(Params& p) {
  ImGui::SliderFloat3("light position", &p.lightPosition.x, -10.0f, 10.0f);
  ImGui::ColorEdit3("light color", &p.lightColor.x);
  ImGui::SliderFloat("light power", &p.lightPower, 0.0f, 5.0f);
  ImGui::SliderFloat("ambient", &p.ambient, 0.0f, 1.0f);
  ImGui::SliderFloat("specular", &p.specular, 0.0f, 5.0f);
  ImGui::Checkbox("spotlight", &p.spotlight);
  ImGui::SameLine();
  ImGui::Checkbox("PCF", &p.pcf);
  ImGui::SliderFloat("bias", &p.bias, 0.0f, 0.05f, "%.4f");
  const int oldSize = p.shadowMapSize;
  ImGui::Combo("shadow map", &p.shadowMapSize, "512\0" "1024\0" "2048\0" "4096\0");
  ImGui::Checkbox("show shadow map", &p.showShadowMap);
  return p.shadowMapSize != oldSize;
}

// World -> light clip space. `depth` picks the API's clip-space z range, like Camera::projection.
inline glm::mat4 lightViewProjection(const Params& p, ClipDepth depth) {
  const bool zo = depth == ClipDepth::ZeroToOne;
  if (p.spotlight) {
    // (The original passed 45 to glm::perspective, which expects radians.)
    const glm::mat4 proj = zo ? glm::perspectiveRH_ZO(glm::radians(45.0f), 1.0f, 2.0f, 50.0f)
                              : glm::perspectiveRH_NO(glm::radians(45.0f), 1.0f, 2.0f, 50.0f);
    return proj * glm::lookAt(p.lightPosition, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
  }
  const glm::mat4 proj = zo ? glm::orthoRH_ZO(-10.0f, 10.0f, -10.0f, 10.0f, -10.0f, 20.0f)
                            : glm::orthoRH_NO(-10.0f, 10.0f, -10.0f, 10.0f, -10.0f, 20.0f);
  // Directional light: only the direction matters, the "eye" just sits on the line through the origin.
  return proj * glm::lookAt(glm::normalize(p.lightPosition), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

// std140 uniform block of the camera pass.
struct Uniforms {
  glm::mat4 mvp;
  glm::mat4 view;
  glm::mat4 model;
  glm::mat4 shadowMatrix;     // world -> shadow map texture coords + depth (light clip space, remapped)
  glm::vec4 lightDirection;   // xyz = towards the light (world)
  glm::vec4 lightColorPower;  // rgb, a = power
  glm::vec4 material;         // x = ambient, y = specular, z = bias, w = 1: PCF
};

}  // namespace glint::shadow_mapping
