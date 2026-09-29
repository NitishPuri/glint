#pragma once

// 02_cube — shared by gl.cpp and vk.cpp.

#include <imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace glint::cube {

struct Params {
  glm::vec3 clearColor{0.0f, 0.0f, 0.4f};  // opengl-tutorial's dark blue
  float rotationSpeed = 10.0f;             // degrees per second
  bool cullBackFaces = true;
};

inline void paramsUi(Params& p) {
  ImGui::ColorEdit3("clear color", &p.clearColor.x);
  ImGui::SliderFloat("rotation", &p.rotationSpeed, -180.0f, 180.0f, "%.0f deg/s");
  ImGui::Checkbox("cull back faces", &p.cullBackFaces);
}

// Spin around a tilted axis so three faces are visible.
inline glm::mat4 model(float angleDegrees) {
  return glm::rotate(glm::mat4(1.0f), glm::radians(angleDegrees), glm::normalize(glm::vec3(0.5f, 1.0f, 0.0f)));
}

}  // namespace glint::cube
