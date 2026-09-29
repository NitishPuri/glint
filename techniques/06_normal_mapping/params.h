#pragma once

// 06_normal_mapping — shared by gl.cpp and vk.cpp. Uses the same uniform block as common/shading.h.

#include <imgui.h>

#include "../common/shading.h"

namespace glint::normal_mapping {

// Where the camera starts (Glint_gl's per-scene position).
inline const glm::vec3 kCameraHome = glm::vec3(0.0f, 0.0f, 5.0f);

struct Params {
  shading::Light light;
  shading::Material material;
  bool useNormalMap = true;  // off: the plain interpolated normal, to see what the map adds
};

inline void paramsUi(Params& p) {
  shading::lightUi(p.light);
  shading::materialUi(p.material);
  ImGui::Checkbox("normal map", &p.useNormalMap);
}

}  // namespace glint::normal_mapping
