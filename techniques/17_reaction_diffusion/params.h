#pragma once

// 17_reaction_diffusion — shared by gl.cpp and vk.cpp. Gray-Scott reaction-diffusion (GPU Gems 2 ch31's worked
// example) on a wrapping grid, stepped N times per frame, painted with the left mouse. Two step paths with the same
// kernel (shaders/rd.glsl): compute (storage images) and fragment (render-to-texture feedback, the 2005 way).
// Each frame a reduction sums v over the grid; the totals come back to the CPU a few frames late, never stalling.

#include <format>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <glm/glm.hpp>
#include <string>

#include "core/input.h"
#include "rd_cpu.h"

namespace glint::rd {

struct Preset {
  const char* name;
  float F, k;
};
// Points in Pearson's (F, k) map (Pearson 1993; values as in Karl Sims' tutorial).
inline constexpr Preset kPresets[] = {{"coral", 0.0545f, 0.062f},  {"mitosis", 0.0367f, 0.0649f},
                                      {"spots", 0.035f, 0.065f},   {"maze", 0.029f, 0.057f},
                                      {"worms", 0.078f, 0.061f},   {"holes", 0.039f, 0.058f}};
inline constexpr int kSizes[] = {256, 512, 1024};
inline constexpr const char* kSizeNames[] = {"256²", "512²", "1024²"};
inline constexpr const char* kPaths[] = {"compute (storage images)", "fragment (render-to-texture, 2005)"};
inline constexpr int kGroup = 16;          // local_size of step/seed/reduce
inline constexpr int kHistory = 240;       // total-v samples kept for the plot
inline constexpr glm::vec4 kDiffusion{1.0f, 0.5f, 1.0f, 0.0f};  // Du, Dv, dt

struct Params {
  int preset = 0;
  float F = kPresets[0].F, k = kPresets[0].k;
  int size = 1;
  int path = 0;
  int stepsPerFrame = 8;
  bool paused = false;
  float brushRadius = 8.0f;
  int palette = 0;
  bool reset = true;   // one-shot: re-seed (the first frame seeds)
  bool verify = false; // one-shot: CPU check of a block (see AutoVerify)

  glm::ivec2 grid() const { return glm::ivec2(kSizes[size]); }
  cpu::Model model() const { return {F, k, kDiffusion.x, kDiffusion.y, kDiffusion.z}; }
};

// Totals of v as they come back from the GPU (a few frames late), for the plot.
struct History {
  std::array<float, kHistory> values{};
  int count = 0;
  void push(float v) {
    std::rotate(values.begin(), values.begin() + 1, values.end());
    values.back() = v;
    count = std::min(count + 1, kHistory);
  }
};

inline void paramsUi(Params& p, const History& h, int latency, const std::string& verifyResult) {
  if (ImGui::Combo("preset", &p.preset, [](void*, int i) { return kPresets[i].name; }, nullptr,
                   IM_ARRAYSIZE(kPresets))) {
    p.F = kPresets[p.preset].F;
    p.k = kPresets[p.preset].k;
  }
  ImGui::SliderFloat("F (feed)", &p.F, 0.0f, 0.1f, "%.4f");
  ImGui::SliderFloat("k (kill)", &p.k, 0.03f, 0.07f, "%.4f");
  if (ImGui::Combo("grid", &p.size, kSizeNames, IM_ARRAYSIZE(kSizeNames))) p.reset = true;
  ImGui::Combo("step path", &p.path, kPaths, IM_ARRAYSIZE(kPaths));
  ImGui::SliderInt("steps / frame", &p.stepsPerFrame, 1, 64);
  ImGui::Checkbox("paused", &p.paused);
  ImGui::SameLine();
  if (ImGui::Button("reset")) p.reset = true;
  ImGui::SliderFloat("brush radius", &p.brushRadius, 2.0f, 40.0f);
  ImGui::Combo("palette", &p.palette, "ink\0gray\0");
  ImGui::TextDisabled("left mouse: paint v");
  ImGui::SeparatorText("total v (GPU reduction, read back without waiting)");
  ImGui::PlotLines("##total", h.values.data() + (kHistory - h.count), h.count, 0, nullptr, FLT_MAX, FLT_MAX,
                   ImVec2(-1, 80));
  ImGui::Text("latest %.1f, arrives %d frame(s) after it was computed", h.count ? h.values.back() : 0.0f, latency);
  if (ImGui::Button("verify on CPU")) p.verify = true;
  if (!verifyResult.empty()) ImGui::TextWrapped("%s", verifyResult.c_str());
}

// --- push constants ----------------------------------------------------------------------------------------------
struct StepConstants {
  glm::ivec4 size;   // x, y = grid, z = 1: brush active
  glm::vec4 params;  // F, k, Du, Dv
  glm::vec4 brush;   // xy = center (texels), z = radius, w = dt
};
struct DisplayConstants {
  glm::vec4 view;   // x = aspect, y = palette
  glm::vec4 brush;  // xy = cursor (texels), z = radius, w = 1: show
};
struct ReduceConstants {
  glm::uvec4 info;  // x, y = grid, z = pass, w = result slot
};

// The brush is applied in the first step of a frame only (so its strength doesn't depend on steps/frame).
inline StepConstants makeStepConstants(const Params& p, bool brush, glm::vec2 brushTexel) {
  const glm::ivec2 g = p.grid();
  return {glm::ivec4(g, brush ? 1 : 0, 0), glm::vec4(p.F, p.k, kDiffusion.x, kDiffusion.y),
          glm::vec4(brushTexel, p.brushRadius, kDiffusion.z)};
}

inline glm::uvec2 groupCounts(glm::ivec2 grid) { return glm::uvec2((grid + kGroup - 1) / kGroup); }

// Window pixel (origin top-left, as GLFW reports it) → grid texel, inverting display.frag's fit-to-square.
inline glm::vec2 screenToTexel(glm::vec2 mouse, glm::ivec2 framebuffer, glm::ivec2 grid) {
  const glm::vec2 fb(framebuffer);
  const glm::vec2 ndc(mouse.x / fb.x * 2.0f - 1.0f, 1.0f - mouse.y / fb.y * 2.0f);
  const float aspect = fb.x / fb.y;
  glm::vec2 q = aspect > 1.0f ? glm::vec2(ndc.x * aspect, ndc.y) : glm::vec2(ndc.x, ndc.y / aspect);
  q /= 0.95f;
  return (q * 0.5f + 0.5f) * glm::vec2(grid);
}

// Painting: left button, and not while the mouse is over an ImGui window.
inline bool painting(const Input& input) { return input.mouseDown(0) && !ImGui::GetIO().WantCaptureMouse; }

// --- the startup check --------------------------------------------------------------------------------------------
// Each path is checked once after a reset: frame 5 resets with the compute path, frame 9 reads back a block;
// frame 10 resets with the fragment path, frame 14 reads back; frame 15 restores the user's path and resets.
// No brush during the check. The readback records how many steps had run since the reset.
inline constexpr int kCheckBlock = 8;
struct AutoVerify {
  int frame = 0, savedPath = 0;
  void step(Params& p) {
    ++frame;
    if (frame == 5 || frame == 10) {
      if (frame == 5) savedPath = p.path;
      p.path = frame == 5 ? 0 : 1;
      p.reset = true;
    } else if (frame == 9 || frame == 14) {
      p.verify = true;
    } else if (frame == 15) {
      p.path = savedPath;
      p.reset = true;
    }
  }
  bool active() const { return frame >= 5 && frame < 15; }
};

// The checked block sits on the seed square nearest the grid's center: a block of untouched cells (u = 1, v = 0
// forever) would match the reference trivially. (The first version checked the center and "passed" with error 0.)
inline glm::ivec2 checkOrigin(glm::ivec2 grid) {
  const glm::ivec2 cells = grid / 32;
  for (int r = 0; r < cells.x; ++r)  // rings of cells around the center
    for (int cy = cells.y / 2 - r; cy <= cells.y / 2 + r; ++cy)
      for (int cx = cells.x / 2 - r; cx <= cells.x / 2 + r; ++cx) {
        if (cx < 0 || cy < 0 || cx >= cells.x || cy >= cells.y) continue;
        const uint32_t h = cpu::pcg(uint32_t(cx) + cpu::pcg(uint32_t(cy) + 17u));
        if (h % 6u == 0u) return glm::ivec2(cx, cy) * 32 + glm::ivec2(10 + int(h % 8u), 10 + int((h >> 3) % 8u));
      }
  return grid / 2 - kCheckBlock / 2;
}

// `texels`: kCheckBlock² RGBA floats, x fastest, read back after `steps` steps from the seed.
inline std::string verifyBlock(const glm::vec4* texels, const Params& p, int steps, int path) {
  const glm::ivec2 origin = checkOrigin(p.grid());
  const auto expected = cpu::simulateBlock(p.grid(), origin, kCheckBlock, steps, p.model());
  float worst = 0;
  for (int i = 0; i < kCheckBlock * kCheckBlock; ++i) {
    const glm::vec2 e = glm::abs(glm::vec2(texels[i]) - expected[size_t(i)]);
    worst = std::max({worst, e.x, e.y});
  }
  float activity = 0;  // how far the block is from the untouched state: proves the check is looking at something
  for (const glm::vec2& e : expected) activity = std::max(activity, e.y);
  return std::format("{} ({} path): {}² block at ({}, {}) after {} steps vs the C++ reference: max error {:.2e} "
                     "(max v there {:.3f})",
                     worst < 1e-4f && activity > 0.05f ? "OK" : "FAIL", path == 0 ? "compute" : "fragment", kCheckBlock,
                     origin.x, origin.y, steps, worst, activity);
}

}  // namespace glint::rd
