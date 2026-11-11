#pragma once

// 18_grass — shared by gl.cpp and vk.cpp. A field of grass blades drawn with *one instanced draw*: a vertex buffer
// holds a single blade (a triangle strip), an instance buffer holds one record per blade (root, facing, height,
// width, lean, random seed), and the GPU runs the strip once per record. The wind is 16's noise: 16's own
// generate.comp writes a tiling 3D texture once, and the grass vertex shader samples its fBm channel at each blade's
// root, scrolled downwind, with time along z (GPU Gems 1 ch7 §7.4.4, "animation per grass object").

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <numbers>
#include <vector>

#include "../16_noise/params.h"  // noise::Generation, GenerateConstants, cpu::pcg

namespace glint::grass {

inline const glm::vec3 kCameraHome{9.0f, 2.2f, 11.0f};
inline constexpr const char* kViews[] = {"shaded", "wind gusts (fBm at the root)", "instance index"};

// What the instance buffer is generated from. Changing any of it regenerates the buffers.
struct Field {
  float size = 24.0f;    // metres; the field is centred on the origin
  int density = 300;     // blades per square metre
  int segments = 4;      // strip segments per blade: 2 * segments + 1 vertices
  float height = 0.55f;  // metres, mean
  float width = 0.035f;  // metres at the root, mean
  int seed = 1;

  bool operator==(const Field&) const = default;
  int perSide() const { return std::max(1, int(std::lround(size * std::sqrt(float(density))))); }
  int bladeCount() const { return perSide() * perSide(); }
  int stripVertices() const { return 2 * segments + 1; }
};

struct Wind {
  float angle = 35.0f;     // degrees, the direction the wind blows *to*, from +x towards +z
  float strength = 0.6f;   // bend, in blade heights per unit t² (see bladeCurve in grass.vert)
  float gustScale = 14.0f; // metres covered by one tile of the wind texture (4 lattice cells of 16's noise)
  float gustSpeed = 4.0f;  // m/s at which the gust pattern travels downwind
  float evolve = 0.04f;    // texture depths per second: how fast gusts change shape (loops after 1/evolve s)
  float flutter = 0.08f;   // per-blade sine wobble on top
};

struct Params {
  Field field;
  Wind wind;
  float fadeDistance = 45.0f;  // metres: blades thin out and vanish towards this distance (LOD)
  int view = 0;
};

// The wind texture: 16's noise at 64^3, 3 octaves. Only R (fBm) is used. Generated once.
inline noise::Generation windGeneration() {
  noise::Generation g;
  g.size = 0;  // 64^3
  g.period = 4;
  g.octaves = 3;
  g.gain = 0.5f;
  return g;
}

// --- vertex data ------------------------------------------------------------------------------------------------
// Per vertex (binding 0, advances every vertex): which edge (-1 left, +1 right, 0 tip) and the height fraction t.
// Same for every blade, so it's tiny: 2 * segments + 1 vec2s.
inline std::vector<glm::vec2> bladeStrip(int segments) {
  std::vector<glm::vec2> strip;
  for (int i = 0; i < segments; ++i) {
    const float t = float(i) / float(segments);
    strip.emplace_back(-1.0f, t);
    strip.emplace_back(1.0f, t);
  }
  strip.emplace_back(0.0f, 1.0f);  // the tip: one vertex closes the last triangle
  return strip;
}

// Per instance (binding 1, advances once per blade): 32 bytes = two vec4 attributes.
struct Blade {
  glm::vec4 rootAngle;  // xyz root position, w facing angle (radians)
  glm::vec4 shape;      // x height, y width, z lean (forward curl), w seed in [0, 1) (LOD order, colour, flutter phase)
};
static_assert(sizeof(Blade) == 32);

// A jittered grid: one blade per cell at a random spot inside it (stratified: no clumps, no gaps). Deterministic
// (pcg hashes of the cell index), so GL and VK build identical buffers.
inline std::vector<Blade> generateBlades(const Field& f) {
  using noise::cpu::pcg;
  const int n = f.perSide();
  const float cell = f.size / float(n);
  const uint32_t salt = pcg(uint32_t(f.seed));
  std::vector<Blade> blades;
  blades.reserve(size_t(n) * size_t(n));
  for (int j = 0; j < n; ++j) {
    for (int i = 0; i < n; ++i) {
      const auto index = uint32_t(j * n + i);
      auto random = [&](uint32_t k) { return float(pcg(index * 8u + k + salt) >> 8) / 16777216.0f; };  // [0, 1)
      const float x = -0.5f * f.size + (float(i) + random(0)) * cell;
      const float z = -0.5f * f.size + (float(j) + random(1)) * cell;
      const float angle = random(2) * 2.0f * std::numbers::pi_v<float>;
      const float height = f.height * (0.6f + 0.8f * random(3));
      const float width = f.width * (0.7f + 0.6f * random(4));
      const float lean = 0.1f + 0.4f * random(5);
      blades.push_back({glm::vec4(x, 0.0f, z, angle), glm::vec4(height, width, lean, random(6))});
    }
  }
  return blades;
}

// --- push constants (GL: the uniform block at binding 15), 128 bytes: the minimum VK guarantees ------------------
struct DrawConstants {
  glm::mat4 viewProjection;
  glm::vec4 eye;    // xyz camera position, w time (s)
  glm::vec4 wind;   // xy direction (world x, z), z strength, w flutter
  glm::vec4 gust;   // x gust scale (m), y gust speed (m/s), z evolve (texture depths/s), w fade distance (m)
  glm::vec4 field;  // x ground half size (m; far enough for the fog to reach the sky colour), y view, z blade count
};
static_assert(sizeof(DrawConstants) == 128);

inline DrawConstants makeDrawConstants(const Params& p, const glm::mat4& viewProjection, glm::vec3 eye, float time,
                                       int bladeCount) {
  const float a = glm::radians(p.wind.angle);
  const Wind& w = p.wind;
  return {viewProjection, glm::vec4(eye, time), glm::vec4(std::cos(a), std::sin(a), w.strength, w.flutter),
          glm::vec4(w.gustScale, w.gustSpeed, w.evolve, p.fadeDistance),
          glm::vec4(std::max(400.0f, p.field.size), float(p.view), float(bladeCount), 0.0f)};
}

inline void paramsUi(Params& p) {
  Field& f = p.field;
  ImGui::SeparatorText("field (regenerates the instance buffer)");
  ImGui::SliderFloat("size (m)", &f.size, 4.0f, 60.0f, "%.0f");
  ImGui::SliderInt("density (/m²)", &f.density, 10, 600);
  ImGui::SliderInt("segments", &f.segments, 1, 8);
  ImGui::SliderFloat("height (m)", &f.height, 0.1f, 1.5f);
  ImGui::SliderFloat("width (m)", &f.width, 0.005f, 0.1f);
  ImGui::SliderInt("seed", &f.seed, 0, 100);
  ImGui::Text("%d blades x %d vertices = %.1fM vertices / draw", f.bladeCount(), f.stripVertices(),
              double(f.bladeCount()) * f.stripVertices() / 1e6);
  ImGui::SeparatorText("wind (16's fBm, sampled per blade)");
  ImGui::SliderFloat("direction (deg)", &p.wind.angle, -180.0f, 180.0f, "%.0f");
  ImGui::SliderFloat("strength", &p.wind.strength, 0.0f, 2.0f);
  ImGui::SliderFloat("gust scale (m)", &p.wind.gustScale, 2.0f, 40.0f);
  ImGui::SliderFloat("gust speed (m/s)", &p.wind.gustSpeed, 0.0f, 12.0f);
  ImGui::SliderFloat("evolve", &p.wind.evolve, 0.0f, 0.2f);
  ImGui::SliderFloat("flutter", &p.wind.flutter, 0.0f, 0.3f);
  ImGui::SeparatorText("display");
  ImGui::SliderFloat("fade distance (m)", &p.fadeDistance, 5.0f, 100.0f, "%.0f");
  ImGui::Combo("view", &p.view, kViews, IM_ARRAYSIZE(kViews));
}

}  // namespace glint::grass
