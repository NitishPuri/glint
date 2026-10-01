#pragma once

// 11_gltf — shared by gl.cpp and vk.cpp. A glTF scene (FlightHelmet: 6 meshes, 6 materials) loaded once by
// core/gltf into CPU data, then drawn with base color + normal maps and one directional light.
// The interesting part is *how often* each piece of data changes and where each API puts it:
//   per frame    (camera, light)          GL: UBO binding 0       VK: descriptor set 0 (one per frame slot)
//   per material (2 textures)             GL: texture units 0-1   VK: descriptor set 1 (one per material)
//   per draw     (model matrix, factors)  GL: uniforms 0-2        VK: push constants

#include <imgui.h>

#include <glm/glm.hpp>
#include <stdexcept>

#include "core/camera.h"
#include "core/gltf.h"

namespace glint::gltf_scene {

inline constexpr const char* kModelPath = "gltf/FlightHelmet/FlightHelmet.gltf";

struct Params {
  glm::vec3 toLight{0.4f, 0.8f, 0.6f};  // direction *towards* the light, world space
  float ambient = 0.25f;
  bool normalMaps = true;
};

inline void paramsUi(Params& p, const ModelData& model) {
  ImGui::Text("%zu draws, %zu materials, %zu triangles", model.draws.size(), model.materials.size(),
              model.triangleCount());
  ImGui::SliderFloat3("to light", &p.toLight.x, -1.0f, 1.0f);
  ImGui::SliderFloat("ambient", &p.ambient, 0.0f, 1.0f);
  ImGui::Checkbox("normal maps", &p.normalMaps);
}

// Per frame (std140).
struct FrameUniforms {
  glm::mat4 viewProjection;
  glm::vec4 toLight;         // xyz normalized
  glm::vec4 cameraPosition;  // xyz
  glm::vec4 settings;        // x = ambient, y = 1: use normal maps
};

// Per draw: 96 bytes — fits VK's guaranteed 128-byte push constant budget.
struct DrawConstants {
  glm::mat4 model;
  glm::vec4 baseColorFactor;
  glm::vec4 material;  // x = alpha cutoff (0 = opaque), y = 1: has a normal map
};
static_assert(sizeof(DrawConstants) <= 128, "push constants: 128 bytes is all VK guarantees");

inline FrameUniforms makeFrameUniforms(const Camera& camera, float aspect, ClipDepth depth, const Params& p) {
  return {camera.viewProjection(aspect, depth), glm::vec4(glm::normalize(p.toLight), 0.0f),
          glm::vec4(camera.eye, 1.0f), glm::vec4(p.ambient, p.normalMaps ? 1.0f : 0.0f, 0.0f, 0.0f)};
}

inline DrawConstants makeDrawConstants(const ModelData& model, const GltfDraw& draw) {
  const int m = model.primitives[size_t(draw.primitive)].material;
  const GltfMaterial& mat = m >= 0 ? model.materials[size_t(m)] : GltfMaterial{};
  return {draw.world, mat.baseColorFactor,
          glm::vec4(mat.alphaMask ? mat.alphaCutoff : 0.0f, mat.normalImage >= 0 ? 1.0f : 0.0f, 0.0f, 0.0f)};
}

// Every primitive gets the same four attributes, so one vertex layout / pipeline serves all of them.
inline void fillMissingAttributes(ModelData& model) {
  for (GltfPrimitive& p : model.primitives) {
    MeshData& m = p.mesh;
    if (m.normals.empty()) throw std::runtime_error("11_gltf: primitive without normals");
    if (m.uvs.empty()) m.uvs.assign(m.vertexCount(), glm::vec2(0.0f));
    if (m.tangents.empty()) computeTangents(m);
  }
}

// Frame the whole model: look at the center of its bounds from the front (+Z), a bit above.
inline void frameModel(Camera& camera, const ModelData& model) {
  const glm::vec3 center = (model.boundsMin + model.boundsMax) * 0.5f;
  const float radius = glm::length(model.boundsMax - model.boundsMin) * 0.5f;
  camera.setHome(center + glm::vec3(0.0f, 0.3f, 2.4f) * radius, center);
}

}  // namespace glint::gltf_scene
