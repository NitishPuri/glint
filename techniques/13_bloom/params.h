#pragma once

// 13_bloom — shared by gl.cpp and vk.cpp. 12's HDR room, plus:
//   bloom:   prefilter (soft threshold) -> downsample through a mip chain -> upsample back, adding each level
//            (Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare", SIGGRAPH 2014 —
//            the modern form of GPU Gems 1 ch21's glow: wide blur = many cheap small blurs at low resolution)
//   grading: color controls (GG1 ch22) baked on the CPU into a 32^3 lookup table (GG2 ch24), applied with one
//            texture lookup after tone mapping.
// The LUT is a 2D strip (32 slices of 32x32 side by side), not a 3D texture: 3D textures arrive in 16_noise.

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

#include "../12_hdr_tonemapping/params.h"
#include "core/assets.h"

namespace glint::bloom {

inline constexpr int kMaxLevels = 6;  // bloom mips: 1/2, 1/4 ... 1/64 of the screen
inline constexpr int kLutSize = 32;   // LUT entries per axis; the strip texture is (32*32) x 32
inline constexpr const char* kViews[] = {"final", "bloom only", "no bloom"};

// Color controls, GG1 ch22 style. All of them become one LUT lookup in the shader, however many there are.
struct Grading {
  bool enabled = true;
  float saturation = 1.15f;   // 0 = grayscale, 1 = unchanged
  float contrast = 1.1f;      // around mid-gray 0.5 (display space)
  float temperature = 0.15f;  // -1 cool (blue) .. +1 warm (orange)
  float gamma = 1.0f;         // midtones: < 1 brighter, > 1 darker

  bool operator==(const Grading&) const = default;
};

struct Params {
  hdr::Params scene;  // 12's lights, exposure and tone mapping (linear workflow only)
  bool bloom = true;
  float threshold = 2.0f;  // radiance where bloom starts (0: everything blooms, "physically based" bloom)
  float knee = 1.0f;       // soft transition around the threshold
  float intensity = 0.8f;
  float radius = 1.0f;  // upsample tent filter radius, in source texels
  int levels = kMaxLevels;
  bool karis = true;  // Karis average in the first downsample: tames single-pixel fireflies
  int view = 0;       // index into kViews
  Grading grading;
};

inline void paramsUi(Params& p) {
  ImGui::SliderFloat("exposure", &p.scene.exposureEv, -6.0f, 6.0f, "%+.1f EV");
  ImGui::Combo("tone mapping", &p.scene.tonemap, hdr::kOperators, IM_ARRAYSIZE(hdr::kOperators));
  ImGui::Combo("view", &p.view, kViews, IM_ARRAYSIZE(kViews));
  ImGui::SeparatorText("bloom");
  ImGui::Checkbox("bloom", &p.bloom);
  ImGui::SliderFloat("threshold", &p.threshold, 0.0f, 10.0f, "%.2f");
  ImGui::SliderFloat("knee", &p.knee, 0.0f, 2.0f, "%.2f");
  ImGui::SliderFloat("intensity", &p.intensity, 0.0f, 4.0f, "%.2f");
  ImGui::SliderFloat("radius", &p.radius, 0.5f, 3.0f, "%.2f texels");
  ImGui::SliderInt("mip levels", &p.levels, 1, kMaxLevels);
  ImGui::Checkbox("Karis average (first downsample)", &p.karis);
  ImGui::SeparatorText("color grading (baked into a LUT)");
  ImGui::Checkbox("grading", &p.grading.enabled);
  ImGui::SliderFloat("saturation", &p.grading.saturation, 0.0f, 2.0f);
  ImGui::SliderFloat("contrast", &p.grading.contrast, 0.5f, 2.0f);
  ImGui::SliderFloat("temperature", &p.grading.temperature, -1.0f, 1.0f);
  ImGui::SliderFloat("gamma", &p.grading.gamma, 0.5f, 2.0f);
  ImGui::SeparatorText("lights");
  for (int i = 0; i < hdr::kLights; ++i) {
    ImGui::PushID(i);
    ImGui::SliderFloat("##intensity", &p.scene.lights[size_t(i)].intensity, 0.0f, 200.0f, "light %.0f");
    ImGui::SameLine();
    ImGui::ColorEdit3("##color", &p.scene.lights[size_t(i)].color.x, ImGuiColorEditFlags_NoInputs);
    ImGui::PopID();
  }
}

// --- push constants (std140 in GL's binding-15 block, plain offsets in VK: vec4s keep both identical) -----

struct DownsampleConstants {
  glm::vec4 params;  // x = 1: first pass (prefilter: threshold + Karis), y = threshold, z = knee, w = Karis on
};

struct UpsampleConstants {
  glm::vec4 params;  // x = filter radius in source texels
};

struct CompositeConstants {
  glm::vec4 tonemap;  // x = exposure scale, y = operator, z = 1: apply the grading LUT
  glm::vec4 bloom;    // x = bloom scale (0: off), y = view
};

inline DownsampleConstants makeDownsampleConstants(const Params& p, bool first) {
  return {glm::vec4(first ? 1.0f : 0.0f, p.threshold, p.knee, p.karis ? 1.0f : 0.0f)};
}

inline CompositeConstants makeCompositeConstants(const Params& p) {
  // Upsampling adds every level on top of the next, so mip 0 holds the sum of `levels` blurred copies:
  // divide to keep `intensity` independent of the level count.
  const float bloomScale = p.bloom ? p.intensity / float(p.levels) : 0.0f;
  return {glm::vec4(std::exp2(p.scene.exposureEv), float(p.scene.tonemap), p.grading.enabled ? 1.0f : 0.0f, 0.0f),
          glm::vec4(bloomScale, float(p.view), 0.0f, 0.0f)};
}

// Size of bloom level `i` for a screen of `size`: level 0 is half resolution, each next one halves again
// (rounding down, like mip levels do in both APIs).
inline glm::ivec2 levelSize(glm::ivec2 size, int i) { return glm::max(size >> (i + 1), glm::ivec2(1)); }

// --- the grading LUT ---------------------------------------------------------------------------------------

// One color through the controls. Input and output are display-encoded (post tone map, sRGB) values in [0,1],
// which is where graders work and where a 32^3 table is precise enough.
inline glm::vec3 grade(glm::vec3 c, const Grading& g) {
  // White balance: a crude warm/cool tint (scale red and blue in opposite directions).
  c *= glm::vec3(1.0f + 0.1f * g.temperature, 1.0f, 1.0f - 0.1f * g.temperature);
  // Saturation: lerp from the luminance (Rec. 709 weights, GG1 ch22 uses the same idea).
  const float luma = glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f));
  c = glm::mix(glm::vec3(luma), c, g.saturation);
  // Contrast around mid-gray, then a midtone gamma.
  c = (c - 0.5f) * g.contrast + 0.5f;
  c = glm::pow(glm::clamp(c, 0.0f, 1.0f), glm::vec3(g.gamma));
  return c;
}

// The LUT as an image: texel (x, y) of the (N*N) x N strip holds grade(r, g, b) with r = (x % N) / (N-1),
// g = y / (N-1), b = (x / N) / (N-1). Row 0 is uploaded first, so v = 0 is g = 0 in GL and VK alike.
// RGBA8 UNORM: the values are already display-encoded, so no sRGB decode on sampling.
inline ImageData makeGradingLut(const Grading& g) {
  constexpr int n = kLutSize;
  ImageData image{n * n, n, 4, std::vector<uint8_t>(size_t(n * n * n * 4))};
  for (int y = 0; y < n; ++y) {
    for (int x = 0; x < n * n; ++x) {
      const glm::vec3 in = glm::vec3(float(x % n), float(y), float(x / n)) / float(n - 1);
      const glm::vec3 out = grade(in, g);
      uint8_t* texel = &image.pixels[size_t((y * n * n + x) * 4)];
      for (int c = 0; c < 3; ++c) texel[c] = uint8_t(std::lround(std::clamp(out[c], 0.0f, 1.0f) * 255.0f));
      texel[3] = 255;
    }
  }
  return image;
}

}  // namespace glint::bloom
