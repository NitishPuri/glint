#pragma once

// 04_basic_shading — shared by gl.cpp and vk.cpp.

#include <imgui.h>

#include "../common/shading.h"

namespace glint::basic_shading {

// Where the camera starts (Glint_gl's per-scene position).
inline const glm::vec3 kCameraHome = glm::vec3(0.0f, 0.0f, 5.0f);

struct Params {
  shading::Light light;
  shading::Material material;
};

inline void paramsUi(Params& p) {
  shading::lightUi(p.light);
  shading::materialUi(p.material);
}

}  // namespace glint::basic_shading
