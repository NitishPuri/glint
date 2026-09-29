#pragma once

// 07_render_to_texture — shared by gl.cpp and vk.cpp.

#include <imgui.h>

#include "../common/shading.h"

namespace glint::render_to_texture {

// Where the camera starts (Glint_gl's per-scene position).
inline const glm::vec3 kCameraHome = glm::vec3(0.0f, 0.0f, 5.0f);

struct Params {
  shading::Light light;
  shading::Material material;
  bool showDepth = false;    // pass 2 shows the offscreen depth buffer instead of the color
  float wobble = 1.0f;       // strength of the post-process distortion (0 = plain copy)
  float depthRange = 10.0f;  // distance shown as black in the depth view (near plane = white)
};

inline void paramsUi(Params& p) {
  shading::lightUi(p.light);
  shading::materialUi(p.material);
  ImGui::SliderFloat("wobble", &p.wobble, 0.0f, 3.0f);
  ImGui::Checkbox("show depth buffer", &p.showDepth);
  if (p.showDepth) ImGui::SliderFloat("depth range", &p.depthRange, 1.0f, 100.0f, "%.1f units");
}

// Uniforms of the post-process pass (pass 2), std140 like everything else.
struct PostUniforms {
  glm::vec4 timeWobble;   // x = time (s), y = wobble strength
  glm::vec4 depthParams;  // x = near, y = far, z = visible range, w = 1: show depth
};

}  // namespace glint::render_to_texture
