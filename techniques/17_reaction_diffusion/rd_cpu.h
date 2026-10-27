#pragma once

// The CPU reference for shaders/rd.glsl + common.glsl (seed, one Gray-Scott step), used by the startup check.
// It doesn't simulate the whole grid: after S steps, a B×B block depends only on the (B + 2S)² region around it
// (each step reads one ring of neighbours), so the reference simulates that region and shrinks it by one cell on
// every side per step — a few hundred thousand cell updates instead of S × the grid.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace glint::rd::cpu {

inline uint32_t pcg(uint32_t v) {
  const uint32_t state = v * 747796405u + 2891336453u;
  const uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

inline glm::vec2 seedValue(glm::ivec2 p) {
  const glm::ivec2 cell = p / 32;
  const uint32_t h = pcg(uint32_t(cell.x) + pcg(uint32_t(cell.y) + 17u));
  const glm::ivec2 local = p - cell * 32 - glm::ivec2(10 + int(h % 8u), 10 + int((h >> 3) % 8u));
  if (h % 6u == 0u && local.x >= 0 && local.y >= 0 && local.x < 12 && local.y < 12) {
    const float noise = float(pcg(uint32_t(p.x) * 65537u + uint32_t(p.y)) & 255u) / 255.0f;
    return {0.5f, 0.25f + 0.05f * noise};
  }
  return {1.0f, 0.0f};
}

struct Model {
  float F, k, Du, Dv, dt;
};

// u, v of block [origin, origin + block)² after `steps` steps from the seed (no brush), on a `size` torus.
inline std::vector<glm::vec2> simulateBlock(glm::ivec2 size, glm::ivec2 origin, int block, int steps, Model m) {
  int n = block + 2 * steps;  // the region this block depends on
  glm::ivec2 base = origin - steps;
  auto wrap = [&](glm::ivec2 p) { return (p + size * 64) % size; };
  std::vector<glm::vec2> cur(size_t(n * n));
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x) cur[size_t(y * n + x)] = seedValue(wrap(base + glm::ivec2(x, y)));
  for (int s = 0; s < steps; ++s) {
    const int m2 = n - 2;  // the new region: one ring smaller
    std::vector<glm::vec2> next(size_t(m2 * m2));
    auto at = [&](int x, int y) { return cur[size_t(y * n + x)]; };
    for (int y = 0; y < m2; ++y)
      for (int x = 0; x < m2; ++x) {
        const int cx = x + 1, cy = y + 1;
        const glm::vec2 c = at(cx, cy);
        glm::vec2 lap = -c;
        lap += 0.2f * (at(cx + 1, cy) + at(cx - 1, cy) + at(cx, cy + 1) + at(cx, cy - 1));
        lap += 0.05f * (at(cx + 1, cy + 1) + at(cx - 1, cy + 1) + at(cx + 1, cy - 1) + at(cx - 1, cy - 1));
        float u = c.x, v = c.y;
        const float uvv = u * v * v;
        u += (m.Du * lap.x - uvv + m.F * (1.0f - u)) * m.dt;
        v += (m.Dv * lap.y + uvv - (m.F + m.k) * v) * m.dt;
        next[size_t(y * m2 + x)] = {std::clamp(u, 0.0f, 1.0f), std::clamp(v, 0.0f, 1.0f)};
      }
    cur = std::move(next);
    n = m2;
  }
  return cur;  // n == block now
}

}  // namespace glint::rd::cpu
