#pragma once

// The CPU reference for shaders/noise.glsl: the same functions, line for line, in C++ (float math like the GPU).
// Used by the startup check, which reads a block of voxels back and compares. Keep the two in sync.

#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

namespace glint::noise::cpu {

inline constexpr int kPerm[256] = {
#include "shaders/permutation.inc"
};

inline const glm::vec3 kGrad[16] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1},  {-1, 0, 1},
                                    {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1},
                                    {1, 1, 0}, {0, -1, 1},  {-1, 1, 0}, {0, -1, -1}};

inline int perm(int i) { return kPerm[i & 255]; }
inline int wrap(int i, int period) { return (i + 1024 * period) % period; }  // as in GLSL (no negative %)
inline glm::vec3 fade(glm::vec3 t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
inline float corner(glm::ivec3 c, glm::vec3 offset) {
  return glm::dot(kGrad[perm(perm(perm(c.x) + c.y) + c.z) & 15], offset);
}

inline float inoise(glm::vec3 p, int period) {
  const glm::ivec3 i0 = glm::ivec3(glm::floor(p));
  const glm::vec3 f = p - glm::floor(p);
  const glm::vec3 u = fade(f);
  const glm::ivec3 a{wrap(i0.x, period), wrap(i0.y, period), wrap(i0.z, period)};
  const glm::ivec3 b{wrap(i0.x + 1, period), wrap(i0.y + 1, period), wrap(i0.z + 1, period)};
  using glm::mix;
  using V = glm::vec3;
  return mix(mix(mix(corner({a.x, a.y, a.z}, f), corner({b.x, a.y, a.z}, f - V(1, 0, 0)), u.x),
                 mix(corner({a.x, b.y, a.z}, f - V(0, 1, 0)), corner({b.x, b.y, a.z}, f - V(1, 1, 0)), u.x), u.y),
             mix(mix(corner({a.x, a.y, b.z}, f - V(0, 0, 1)), corner({b.x, a.y, b.z}, f - V(1, 0, 1)), u.x),
                 mix(corner({a.x, b.y, b.z}, f - V(0, 1, 1)), corner({b.x, b.y, b.z}, f - V(1, 1, 1)), u.x), u.y),
             u.z);
}

inline float fbm(glm::vec3 p, int period, int octaves, float gain) {
  float sum = 0, amp = 0.5f;
  for (int o = 0; o < octaves; ++o, p *= 2.0f, period *= 2, amp *= gain) sum += inoise(p, period) * amp;
  return sum;
}

inline float turbulence(glm::vec3 p, int period, int octaves, float gain) {
  float sum = 0, amp = 1.0f;
  for (int o = 0; o < octaves; ++o, p *= 2.0f, period *= 2, amp *= gain) sum += std::abs(inoise(p, period)) * amp;
  return sum;
}

inline float ridgedmf(glm::vec3 p, int period, int octaves, float gain) {
  float sum = 0, amp = 0.5f, prev = 1.0f;
  for (int o = 0; o < octaves; ++o, p *= 2.0f, period *= 2, amp *= gain) {
    float n = 1.0f - std::abs(inoise(p, period));
    n *= n;
    sum += n * amp * prev;
    prev = n;
  }
  return sum;
}

inline uint32_t pcg(uint32_t v) {
  const uint32_t state = v * 747796405u + 2891336453u;
  const uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

inline float worley(glm::vec3 p, int period, uint32_t seed) {
  const glm::ivec3 cell = glm::ivec3(glm::floor(p));
  float best = 1e9f;
  for (int z = -1; z <= 1; ++z)
    for (int y = -1; y <= 1; ++y)
      for (int x = -1; x <= 1; ++x) {
        const glm::ivec3 c = cell + glm::ivec3(x, y, z);
        const glm::ivec3 w{wrap(c.x, period), wrap(c.y, period), wrap(c.z, period)};
        const uint32_t h = pcg(uint32_t(w.x) + pcg(uint32_t(w.y) + pcg(uint32_t(w.z) + seed)));
        const glm::vec3 feature =
            glm::vec3(c) + glm::vec3(float(h & 1023u), float((h >> 10) & 1023u), float((h >> 20) & 1023u)) / 1024.0f;
        const glm::vec3 d = feature - p;
        best = std::fmin(best, glm::dot(d, d));
      }
  return std::sqrt(best);
}

inline glm::vec4 noiseChannels(glm::vec3 p, int period, int octaves, float gain, int worleyCells, uint32_t seed) {
  const float r = glm::clamp(0.5f + 0.7f * fbm(p, period, octaves, gain), 0.0f, 1.0f);
  const float g = glm::clamp(turbulence(p, period, octaves, gain) * 0.6f, 0.0f, 1.0f);
  const float b = glm::clamp(ridgedmf(p, period, octaves, gain), 0.0f, 1.0f);
  const glm::vec3 q = p * (float(worleyCells) / float(period));
  const float a = glm::clamp(1.0f - worley(q, worleyCells, seed) * 1.2f, 0.0f, 1.0f);
  return {r, g, b, a};
}

}  // namespace glint::noise::cpu
