#pragma once

// 16_noise — shared by gl.cpp and vk.cpp. A compute shader *writes a 3D texture* (a storage image) with four
// procedural noises, one per channel: R fBm, G turbulence, B ridged multifractal (improved Perlin noise, GPU Gems
// 2 ch26), A Worley F1. Every noise tiles with the texture. Then a full-screen pass shows either 2D slices of the
// four channels (scrolling through z) or ray-marches one channel as a volume. The texture is regenerated only
// when a generation parameter changes, or every frame with "evolve" on.

#include <format>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <string>

#include "noise_cpu.h"

namespace glint::noise {

inline const glm::vec3 kCameraHome{2.6f, 1.8f, 3.4f};
inline constexpr int kSizes[] = {64, 128, 256};
inline constexpr const char* kSizeNames[] = {"64^3 (2 MB)", "128^3 (16 MB)", "256^3 (128 MB)"};
inline constexpr const char* kChannels[] = {"R: fBm", "G: turbulence", "B: ridged multifractal", "A: Worley F1"};
inline constexpr const char* kModes[] = {"slices (all 4 channels)", "volume (one channel)"};
inline constexpr glm::uvec3 kGroup{8, 8, 4};  // local_size of generate.comp

// Everything that changes the texture's contents.
struct Generation {
  int size = 1;  // index into kSizes
  int period = 4;  // lattice cells across the texture for the base octave (and the tiling period)
  int octaves = 5;
  float gain = 0.5f;
  int worleyCells = 6;
  int seed = 0;
  float evolveOffset = 0.0f;  // domain offset along z, advanced by "evolve"

  bool operator==(const Generation&) const = default;
  int extent() const { return kSizes[size]; }
};

struct Params {
  Generation gen;
  bool evolve = false;
  float evolveSpeed = 0.3f;  // lattice cells per second
  int mode = 0;
  int channel = 0;          // volume mode
  float sliceSpeed = 0.1f;  // slices mode: texture depths per second
  int tiles = 2;            // slices mode: repeat the texture to show it tiles
  float density = 12.0f;    // volume mode
  float coverage = 0.45f;   // volume mode: values below this are empty
  bool verify = false;
};

inline void paramsUi(Params& p, const std::string& verifyResult) {
  Generation& g = p.gen;
  ImGui::SeparatorText("generation (compute -> storage image)");
  ImGui::Combo("size", &g.size, kSizeNames, IM_ARRAYSIZE(kSizeNames));
  ImGui::SliderInt("period (cells)", &g.period, 1, 16);
  ImGui::SliderInt("octaves", &g.octaves, 1, 8);
  ImGui::SliderFloat("gain", &g.gain, 0.2f, 0.8f);
  ImGui::SliderInt("Worley cells", &g.worleyCells, 1, 16);
  ImGui::SliderInt("seed", &g.seed, 0, 100);
  ImGui::Checkbox("evolve (regenerate every frame)", &p.evolve);
  ImGui::SliderFloat("evolve speed", &p.evolveSpeed, 0.0f, 2.0f);
  ImGui::SeparatorText("display");
  ImGui::Combo("mode", &p.mode, kModes, IM_ARRAYSIZE(kModes));
  if (p.mode == 0) {
    ImGui::SliderFloat("slice speed", &p.sliceSpeed, 0.0f, 0.5f);
    ImGui::SliderInt("tiles", &p.tiles, 1, 4);
  } else {
    ImGui::Combo("channel", &p.channel, kChannels, IM_ARRAYSIZE(kChannels));
    ImGui::SliderFloat("density", &p.density, 0.0f, 40.0f);
    ImGui::SliderFloat("coverage", &p.coverage, 0.0f, 1.0f);
  }
  if (ImGui::Button("verify a block on the CPU")) p.verify = true;
  if (!verifyResult.empty()) ImGui::TextWrapped("%s", verifyResult.c_str());
}

// --- push constants -------------------------------------------------------------------------------------------
struct GenerateConstants {
  glm::uvec4 dims;   // x = extent, y = octaves, z = period, w = Worley cells
  glm::vec4 params;  // x = gain, y = z offset, z = seed
};

struct DisplayConstants {
  glm::mat4 inverseViewProjection;
  glm::vec4 eye;
  glm::vec4 view;    // x = mode, y = channel, z = slice depth [0,1), w = aspect
  glm::vec4 volume;  // x = density, y = coverage, z = tiles
};

inline GenerateConstants makeGenerateConstants(const Generation& g) {
  return {glm::uvec4(uint32_t(g.extent()), uint32_t(g.octaves), uint32_t(g.period), uint32_t(g.worleyCells)),
          glm::vec4(g.gain, g.evolveOffset, float(g.seed), 0.0f)};
}

inline DisplayConstants makeDisplayConstants(const Params& p, const glm::mat4& viewProjection, glm::vec3 eye,
                                             float aspect, float time) {
  return {glm::inverse(viewProjection), glm::vec4(eye, 1.0f),
          glm::vec4(float(p.mode), float(p.channel), std::fmod(time * p.sliceSpeed, 1.0f), aspect),
          glm::vec4(p.density, p.coverage, float(p.tiles), 0.0f)};
}

inline glm::uvec3 groupCounts(const Generation& g) {
  const glm::uvec3 n(uint32_t(g.extent()));
  return (n + kGroup - 1u) / kGroup;
}

// Voxel v's sample position: its center, in lattice units (so [0, period) across the texture), plus the evolve
// offset. generate.comp computes exactly this.
inline glm::vec3 samplePosition(glm::ivec3 v, const Generation& g) {
  return (glm::vec3(v) + 0.5f) / float(g.extent()) * float(g.period) + glm::vec3(0.0f, 0.0f, g.evolveOffset);
}

// --- the CPU check: a block of voxels read back vs noise_cpu.h ------------------------------------------------
inline constexpr int kCheckBlock = 8;
inline glm::ivec3 checkOrigin(const Generation& g) { return glm::ivec3(g.extent() / 2 - kCheckBlock / 2); }

// `texels`: kCheckBlock^3 RGBA values as floats, x fastest. Half floats hold ~3 decimal digits: 2e-3 is the budget.
inline std::string verifyBlock(const glm::vec4* texels, const Generation& g) {
  float worst = 0;
  glm::ivec3 worstAt{0};
  const glm::ivec3 origin = checkOrigin(g);
  for (int z = 0; z < kCheckBlock; ++z)
    for (int y = 0; y < kCheckBlock; ++y)
      for (int x = 0; x < kCheckBlock; ++x) {
        const glm::ivec3 v = origin + glm::ivec3(x, y, z);
        const glm::vec4 expected = cpu::noiseChannels(samplePosition(v, g), g.period, g.octaves, g.gain,
                                                      g.worleyCells, uint32_t(g.seed));
        const glm::vec4 got = texels[(z * kCheckBlock + y) * kCheckBlock + x];
        const glm::vec4 err = glm::abs(got - expected);
        const float e = std::max({err.r, err.g, err.b, err.a});
        if (e > worst) worst = e, worstAt = v;
      }
  return std::format("{}: {}^3 voxels at {}, all 4 channels vs the C++ reference: max error {:.5f} at ({}, {}, {})",
                     worst <= 2e-3f ? "OK" : "FAIL", kCheckBlock, origin.x, worst, worstAt.x, worstAt.y, worstAt.z);
}

}  // namespace glint::noise
