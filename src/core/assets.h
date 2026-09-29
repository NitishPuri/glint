#pragma once

// CPU-side mesh and image data. Each backend uploads these its own way (GL: glNamedBufferStorage /
// glTextureSubImage2D; VK: staging buffer + vkCmdCopyBuffer / vkCmdCopyBufferToImage).

#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>
#include <string_view>
#include <vector>

namespace glint {

// Separate arrays per attribute ("structure of arrays"), like opengl-tutorial: easy to read, and each
// backend decides whether to upload them as separate buffers or interleave them.
// All non-empty attribute arrays have vertexCount() entries.
struct MeshData {
  std::vector<glm::vec3> positions;
  std::vector<glm::vec3> normals;   // may be empty
  std::vector<glm::vec2> uvs;       // may be empty; v = 0 at the bottom of the image (OBJ convention)
  std::vector<glm::vec4> tangents;  // may be empty; xyz = tangent, w = ±1: bitangent = cross(normal, tangent) * w
  std::vector<uint32_t> indices;    // triangle list, counter-clockwise front faces

  size_t vertexCount() const { return positions.size(); }
  size_t triangleCount() const { return indices.size() / 3; }
};

// Triangulated OBJ as a flat (non-indexed) triangle list: indices are just 0..n-1, like opengl-tutorial's
// loader. Call indexMesh() to share vertices. Materials (.mtl) are ignored. Throws on failure.
MeshData loadObj(const std::filesystem::path& path);

// Merges vertices whose attributes are bit-for-bit identical and rewrites the indices.
// (opengl-tutorial's indexVBO, but hashed instead of std::map/linear search.)
void indexMesh(MeshData& mesh);

// Per-vertex tangents from positions, normals and uvs. Tangents of triangles that share a vertex are
// averaged, so call it *after* indexMesh() for smooth tangents. Throws if normals or uvs are missing.
void computeTangents(MeshData& mesh);

// Unit cube [-1,1]^3: 24 vertices (4 per face, so each face has its own normal and uvs), 36 indices,
// with normals, uvs and tangents.
MeshData makeCube();
// Quad [-1,1]^2 in the XY plane facing +Z: 4 vertices, 6 indices, with normals, uvs and tangents.
MeshData makeQuad();

// 8-bit image, always expanded to RGBA (channels == 4) so both backends use one format (RGBA8).
struct ImageData {
  int width = 0;
  int height = 0;
  int channels = 0;
  std::vector<uint8_t> pixels;  // rows tightly packed, first row = bottom of the image if flipped

  size_t sizeBytes() const { return pixels.size(); }
};

// flipVertically = true puts the image's bottom row first in memory, so that uv (0,0) — the first texel in
// both GL and VK — samples the bottom-left corner, matching OBJ uvs. Throws on failure.
ImageData loadImage(const std::filesystem::path& path, bool flipVertically = true);

// <asset dir>/<relative>. The asset dir defaults to the repo's assets/ (GLINT_ASSET_DIR), see setAssetDir().
std::filesystem::path assetPath(std::string_view relative);
void setAssetDir(const std::filesystem::path& dir);

}  // namespace glint
