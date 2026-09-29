#pragma once

// 05_indexed_mesh — shared by gl.cpp and vk.cpp.

#include <imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../common/shading.h"

namespace glint::indexed_mesh {

// Where the camera starts (Glint_gl's per-scene position).
inline const glm::vec3 kCameraHome = glm::vec3(0.0f, 0.0f, 5.0f);

struct Params {
  shading::Light light;
  shading::Material material;
  bool transparent = false;  // alpha-blend both monkeys (no sorting: the draw order shows through)
  float alpha = 0.3f;
};

inline void paramsUi(Params& p) {
  shading::lightUi(p.light);
  shading::materialUi(p.material);
  ImGui::Checkbox("transparent", &p.transparent);
  if (p.transparent) ImGui::SliderFloat("alpha", &p.alpha, 0.0f, 1.0f);
}

// Two instances of the same mesh: the second one shifted right, with half-strength material.
inline glm::mat4 model(int instance) {
  return instance == 0 ? glm::mat4(1.0f) : glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, 0.0f, 0.0f));
}

inline shading::Material material(const Params& p, int instance) {
  shading::Material m = p.material;
  if (instance == 1) {
    m.ambient *= 0.5f;
    m.specular *= 0.5f;
  }
  m.alpha = p.transparent ? p.alpha : 1.0f;
  return m;
}

}  // namespace glint::indexed_mesh
