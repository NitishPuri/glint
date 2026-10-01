#pragma once

// glTF 2.0 loading (tinygltf) into plain CPU data that both backends upload their own way — the same idea
// as MeshData/ImageData, one level up: several meshes, materials referencing images, and a node hierarchy
// flattened into "draw this primitive with this world matrix".
//
// Supported: triangle primitives with POSITION (+ NORMAL, TEXCOORD_0, TANGENT), 8/16/32-bit indices,
// node TRS/matrix hierarchies, base color factor/texture, normal texture, doubleSided, alpha MASK cutoff.
// Not (yet): PBR metallic-roughness shading data, skins, animation, morph targets, cameras, lights.

#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <vector>

#include "core/assets.h"

namespace glint {

struct GltfMaterial {
  std::string name;
  glm::vec4 baseColorFactor{1.0f};
  int baseColorImage = -1;  // index into ModelData::images, -1: none (use white)
  int normalImage = -1;     // -1: none (use the vertex normal)
  bool doubleSided = false;  // -> no back-face culling for this material
  bool alphaMask = false;    // alphaMode MASK: discard below alphaCutoff
  float alphaCutoff = 0.5f;
};

struct GltfPrimitive {
  MeshData mesh;      // indexed; normals/uvs/tangents as present in the file
  int material = -1;  // index into ModelData::materials, -1: default material
};

// One instance of a primitive in the scene: node hierarchy already multiplied out.
struct GltfDraw {
  int primitive = 0;
  glm::mat4 world{1.0f};
};

struct ModelData {
  std::vector<GltfPrimitive> primitives;
  std::vector<GltfMaterial> materials;
  std::vector<ImageData> images;  // RGBA8, *not* flipped: glTF's uv (0,0) is the image's top-left texel
  std::vector<GltfDraw> draws;
  glm::vec3 boundsMin{0.0f}, boundsMax{0.0f};  // world-space bounds of all draws

  size_t triangleCount() const;
};

// .gltf (+ .bin + images) or .glb. Only images referenced by a base-color or normal texture are decoded.
// Throws on failure.
ModelData loadGltf(const std::filesystem::path& path);

}  // namespace glint
