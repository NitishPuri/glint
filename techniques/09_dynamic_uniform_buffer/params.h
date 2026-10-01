#pragma once

// 09_dynamic_uniform_buffer — shared by gl.cpp and vk.cpp. From Glint_vk's dynamic_uniform_buffer sample
// (itself after Sascha Willems' "dynamicuniformbuffer"): many objects, one uniform buffer holding every
// object's model matrix, and each draw reading its own slice of it.

#include <imgui.h>

#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <random>

namespace glint::dynamic_ubo {

inline constexpr int kGrid = 5;  // 5 x 5 x 5
inline constexpr int kObjects = kGrid * kGrid * kGrid;
inline const glm::vec3 kCameraHome{30.0f, 22.0f, -36.0f};

struct Params {
  float speed = 1.0f;  // rotation speed multiplier
  bool animate = true;
};

inline void paramsUi(Params& p) {
  ImGui::Checkbox("animate", &p.animate);
  ImGui::SliderFloat("speed", &p.speed, 0.0f, 4.0f);
}

// Per-object random start rotations and speeds. Seeded, so GL and VK animate identically.
struct Objects {
  std::array<glm::vec3, kObjects> rotations;  // radians
  std::array<glm::vec3, kObjects> speeds;     // radians per second

  Objects() {
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (int i = 0; i < kObjects; ++i) {
      rotations[size_t(i)] = glm::vec3(dist(rng), dist(rng), dist(rng)) * 3.14159265f;
      speeds[size_t(i)] = glm::vec3(dist(rng), dist(rng), dist(rng));
    }
  }

  void animate(float dt, const Params& p) {
    if (!p.animate) return;
    for (int i = 0; i < kObjects; ++i) rotations[size_t(i)] += speeds[size_t(i)] * (dt * p.speed);
  }

  // Grid position (5 units apart, centered) * rotation.
  glm::mat4 model(int i) const {
    const int x = i / (kGrid * kGrid), y = (i / kGrid) % kGrid, z = i % kGrid;
    const glm::vec3 position = (glm::vec3(x, y, z) - glm::vec3((kGrid - 1) * 0.5f)) * 5.0f;
    glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
    const glm::vec3& r = rotations[size_t(i)];
    m = glm::rotate(m, r.x, glm::vec3(1, 0, 0));
    m = glm::rotate(m, r.y, glm::vec3(0, 1, 0));
    m = glm::rotate(m, r.z, glm::vec3(0, 0, 1));
    return m;
  }
};

// Round `size` up to a multiple of `alignment` (a power of two in practice, but this works for any).
inline size_t alignUp(size_t size, size_t alignment) { return (size + alignment - 1) / alignment * alignment; }

}  // namespace glint::dynamic_ubo
