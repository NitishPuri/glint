#pragma once

// 01_triangle — shared by gl.cpp and vk.cpp: the knobs (edited by the same ImGui panel in both apps)
// and the vertex data, so both backends draw exactly the same thing.

#include <imgui.h>

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace glint::triangle {

struct Params {
  glm::vec3 clearColor{0.1f, 0.1f, 0.1f};
  float rotationSpeed = 30.0f;  // degrees per second
  bool quad = false;            // draw the quad (6 indices) instead of the triangle (3 indices)
};

inline void paramsUi(Params& p) {
  ImGui::ColorEdit3("clear color", &p.clearColor.x);
  ImGui::SliderFloat("rotation", &p.rotationSpeed, -180.0f, 180.0f, "%.0f deg/s");
  ImGui::Checkbox("quad (6 indices) instead of triangle (3)", &p.quad);
}

// Interleaved x, y, r, g, b — one buffer, two attributes (location 0: vec2 position, location 4: vec3 color).
struct Vertex {
  glm::vec2 position;
  glm::vec3 color;
};

inline constexpr Vertex kVertices[] = {
    {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},  // 0 bottom-left   red
    {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},   // 1 bottom-right  green
    {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},    // 2 top-right     blue
    {{-0.5f, 0.5f}, {1.0f, 1.0f, 0.0f}},   // 3 top-left      yellow
    {{0.0f, 0.5f}, {0.0f, 0.0f, 1.0f}},    // 4 top-middle    blue (triangle apex)
};

// Counter-clockwise when looking at the screen (+Y up), i.e. front-facing in both APIs' default convention.
inline constexpr uint32_t kTriangleIndices[] = {0, 1, 4};
inline constexpr uint32_t kQuadIndices[] = {0, 1, 2, 2, 3, 0};

// Model transform: spin around Z, then undo the window's aspect stretch so the shape keeps its proportions.
inline glm::mat4 transform(float angleDegrees, float aspect) {
  const glm::mat4 fixAspect = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / aspect, 1.0f, 1.0f));
  return fixAspect * glm::rotate(glm::mat4(1.0f), glm::radians(angleDegrees), glm::vec3(0.0f, 0.0f, 1.0f));
}

}  // namespace glint::triangle
