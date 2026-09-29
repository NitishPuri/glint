#include "core/assets.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "core/log.h"
#include "core/timer.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

namespace glint {

// ---------------------------------------------------------------------------------------------- OBJ

MeshData loadObj(const std::filesystem::path& path) {
  ScopedTimer timer(fmt::format("loadObj {}", path.filename().string()));

  tinyobj::ObjReaderConfig config;
  config.triangulate = true;
  config.mtl_search_path = path.parent_path().string();
  tinyobj::ObjReader reader;
  if (!reader.ParseFromFile(path.string(), config)) {
    throw std::runtime_error(fmt::format("loadObj '{}': {}", path.string(), reader.Error()));
  }
  // tinyobjloader's warnings are almost always about missing .mtl files/textures, which we ignore anyway.
  if (!reader.Warning().empty()) log::debug("loadObj '{}': {}", path.string(), reader.Warning());

  const tinyobj::attrib_t& attrib = reader.GetAttrib();
  MeshData mesh;
  size_t withNormal = 0, withUv = 0;

  // OBJ indexes positions, normals and uvs separately; GPUs want one index per vertex. The simplest
  // correct conversion is to emit every corner as its own vertex, then deduplicate in indexMesh().
  for (const tinyobj::shape_t& shape : reader.GetShapes()) {
    for (const tinyobj::index_t& idx : shape.mesh.indices) {
      const float* p = &attrib.vertices[3 * size_t(idx.vertex_index)];
      mesh.positions.emplace_back(p[0], p[1], p[2]);
      if (idx.normal_index >= 0) {
        const float* n = &attrib.normals[3 * size_t(idx.normal_index)];
        mesh.normals.emplace_back(n[0], n[1], n[2]);
        ++withNormal;
      } else {
        mesh.normals.emplace_back(0.0f);
      }
      if (idx.texcoord_index >= 0) {
        const float* t = &attrib.texcoords[2 * size_t(idx.texcoord_index)];
        mesh.uvs.emplace_back(t[0], t[1]);
        ++withUv;
      } else {
        mesh.uvs.emplace_back(0.0f);
      }
      mesh.indices.push_back(uint32_t(mesh.indices.size()));
    }
  }

  // An attribute is either there for every vertex or dropped: half-filled arrays would be garbage on the GPU.
  const size_t count = mesh.vertexCount();
  if (count == 0) throw std::runtime_error(fmt::format("loadObj '{}': no triangles", path.string()));
  if (withNormal != count) {
    if (withNormal > 0) {
      log::warn("loadObj '{}': only {}/{} vertices have normals, dropping them", path.string(), withNormal, count);
    }
    mesh.normals.clear();
  }
  if (withUv != count) {
    if (withUv > 0) {
      log::warn("loadObj '{}': only {}/{} vertices have uvs, dropping them", path.string(), withUv, count);
    }
    mesh.uvs.clear();
  }

  log::info("loadObj {}: {} triangles{}{}", path.filename().string(), mesh.triangleCount(),
            mesh.normals.empty() ? "" : ", normals", mesh.uvs.empty() ? "" : ", uvs");
  return mesh;
}

// --------------------------------------------------------------------------------------- indexing

namespace {

// Every attribute of one vertex, zero where the mesh lacks it. Compared bytewise (like opengl-tutorial's
// memcmp-based PackedVertex), so only exact duplicates merge — which is what shared OBJ corners are.
struct PackedVertex {
  glm::vec3 position{0.0f};
  glm::vec3 normal{0.0f};
  glm::vec2 uv{0.0f};
  glm::vec4 tangent{0.0f};

  bool operator==(const PackedVertex& o) const { return std::memcmp(this, &o, sizeof(*this)) == 0; }
};
static_assert(sizeof(PackedVertex) == 12 * sizeof(float), "no padding, so memcmp/hash see only the floats");

struct PackedVertexHash {
  size_t operator()(const PackedVertex& v) const {
    // FNV-1a over the raw bytes.
    const auto* bytes = reinterpret_cast<const unsigned char*>(&v);
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < sizeof(v); ++i) h = (h ^ bytes[i]) * 1099511628211ull;
    return size_t(h);
  }
};

}  // namespace

void indexMesh(MeshData& mesh) {
  ScopedTimer timer("indexMesh");
  const bool hasN = !mesh.normals.empty(), hasUv = !mesh.uvs.empty(), hasT = !mesh.tangents.empty();

  MeshData out;
  std::unordered_map<PackedVertex, uint32_t, PackedVertexHash> seen;
  seen.reserve(mesh.vertexCount());
  out.indices.reserve(mesh.indices.size());

  for (uint32_t i : mesh.indices) {
    PackedVertex v;
    v.position = mesh.positions[i];
    if (hasN) v.normal = mesh.normals[i];
    if (hasUv) v.uv = mesh.uvs[i];
    if (hasT) v.tangent = mesh.tangents[i];

    auto [it, inserted] = seen.try_emplace(v, uint32_t(out.positions.size()));
    if (inserted) {
      out.positions.push_back(v.position);
      if (hasN) out.normals.push_back(v.normal);
      if (hasUv) out.uvs.push_back(v.uv);
      if (hasT) out.tangents.push_back(v.tangent);
    }
    out.indices.push_back(it->second);
  }

  log::debug("indexMesh: {} -> {} vertices", mesh.vertexCount(), out.vertexCount());
  mesh = std::move(out);
}

// --------------------------------------------------------------------------------------- tangents

// https://terathon.com/blog/tangent-space.html
// https://www.opengl-tutorial.org/intermediate-tutorials/tutorial-13-normal-mapping/
void computeTangents(MeshData& mesh) {
  if (mesh.normals.empty() || mesh.uvs.empty()) throw std::runtime_error("computeTangents: needs normals and uvs");

  const size_t n = mesh.vertexCount();
  std::vector<glm::vec3> tan(n, glm::vec3(0.0f));
  std::vector<glm::vec3> bitan(n, glm::vec3(0.0f));

  // Per triangle: solve  edge = dU * T + dV * B  for T and B, and add them to each corner.
  for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
    const uint32_t i0 = mesh.indices[t], i1 = mesh.indices[t + 1], i2 = mesh.indices[t + 2];
    const glm::vec3 e1 = mesh.positions[i1] - mesh.positions[i0];
    const glm::vec3 e2 = mesh.positions[i2] - mesh.positions[i0];
    const glm::vec2 d1 = mesh.uvs[i1] - mesh.uvs[i0];
    const glm::vec2 d2 = mesh.uvs[i2] - mesh.uvs[i0];
    const float det = d1.x * d2.y - d2.x * d1.y;
    if (std::abs(det) < 1e-12f) continue;  // degenerate uvs: no information about the tangent direction
    const float r = 1.0f / det;
    const glm::vec3 T = (e1 * d2.y - e2 * d1.y) * r;
    const glm::vec3 B = (e2 * d1.x - e1 * d2.x) * r;
    for (uint32_t i : {i0, i1, i2}) {
      tan[i] += T;
      bitan[i] += B;
    }
  }

  mesh.tangents.resize(n);
  for (size_t i = 0; i < n; ++i) {
    const glm::vec3 N = mesh.normals[i];
    // Gram-Schmidt: make T perpendicular to N.
    glm::vec3 T = tan[i] - N * glm::dot(N, tan[i]);
    if (glm::dot(T, T) < 1e-12f) {
      // No usable uvs around this vertex: any unit vector perpendicular to N will do.
      T = glm::cross(N, std::abs(N.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0));
    }
    T = glm::normalize(T);
    // Handedness: mirrored uvs make the real bitangent point opposite to cross(N, T). The shader rebuilds
    // B = cross(N, T) * w instead of storing a whole extra vec3 (opengl-tutorial stores B and flips T).
    const float w = glm::dot(glm::cross(N, T), bitan[i]) < 0.0f ? -1.0f : 1.0f;
    mesh.tangents[i] = glm::vec4(T, w);
  }
}

// ------------------------------------------------------------------------------------- primitives

namespace {

// One face: center c = normal, spanned by u (uv x) and v (uv y). cross(u, v) == normal keeps it CCW.
void addFace(MeshData& m, glm::vec3 normal, glm::vec3 u, glm::vec3 v) {
  const uint32_t base = uint32_t(m.positions.size());
  const glm::vec2 corners[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  for (glm::vec2 c : corners) {
    m.positions.push_back(normal + u * (c.x * 2.0f - 1.0f) + v * (c.y * 2.0f - 1.0f));
    m.normals.push_back(normal);
    m.uvs.push_back(c);
    m.tangents.emplace_back(u, 1.0f);  // cross(normal, u) == v, so w = +1
  }
  for (uint32_t i : {0u, 1u, 2u, 2u, 3u, 0u}) m.indices.push_back(base + i);
}

}  // namespace

MeshData makeCube() {
  MeshData m;
  addFace(m, {1, 0, 0}, {0, 0, -1}, {0, 1, 0});   // +X
  addFace(m, {-1, 0, 0}, {0, 0, 1}, {0, 1, 0});   // -X
  addFace(m, {0, 1, 0}, {1, 0, 0}, {0, 0, -1});   // +Y
  addFace(m, {0, -1, 0}, {1, 0, 0}, {0, 0, 1});   // -Y
  addFace(m, {0, 0, 1}, {1, 0, 0}, {0, 1, 0});    // +Z
  addFace(m, {0, 0, -1}, {-1, 0, 0}, {0, 1, 0});  // -Z
  return m;
}

MeshData makeQuad() {
  MeshData m;
  addFace(m, {0, 0, 1}, {1, 0, 0}, {0, 1, 0});
  for (glm::vec3& p : m.positions) p.z = 0.0f;  // addFace offsets by the normal; the quad sits at z = 0
  return m;
}

// ----------------------------------------------------------------------------------------- images

ImageData loadImage(const std::filesystem::path& path, bool flipVertically) {
  int w = 0, h = 0, fileChannels = 0;
  stbi_uc* data = stbi_load(path.string().c_str(), &w, &h, &fileChannels, 4);
  if (!data) throw std::runtime_error(fmt::format("loadImage '{}': {}", path.string(), stbi_failure_reason()));

  ImageData img;
  img.width = w;
  img.height = h;
  img.channels = 4;
  img.pixels.resize(size_t(w) * h * 4);
  const size_t rowBytes = size_t(w) * 4;
  // Flip by copying rows in reverse (instead of stbi_set_flip_vertically_on_load, which is global state).
  for (int y = 0; y < h; ++y) {
    const int srcRow = flipVertically ? h - 1 - y : y;
    std::memcpy(img.pixels.data() + y * rowBytes, data + srcRow * rowBytes, rowBytes);
  }
  stbi_image_free(data);

  log::info("loadImage {}: {}x{} ({} channels in file)", path.filename().string(), w, h, fileChannels);
  return img;
}

void writePng(const std::filesystem::path& path, const ImageData& image, bool flipVertically) {
  if (image.channels != 4 || image.pixels.size() != size_t(image.width) * image.height * 4) {
    throw std::runtime_error("writePng: expects tightly packed RGBA8");
  }
  stbi_flip_vertically_on_write(flipVertically ? 1 : 0);
  if (!stbi_write_png(path.string().c_str(), image.width, image.height, 4, image.pixels.data(), image.width * 4)) {
    throw std::runtime_error(fmt::format("writePng '{}' failed", path.string()));
  }
  stbi_flip_vertically_on_write(0);
}

std::string readTextFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error(fmt::format("cannot open '{}'", path.string()));
  std::ostringstream text;
  text << file.rdbuf();
  return text.str();
}

// ------------------------------------------------------------------------------------------ paths

namespace {
std::filesystem::path g_assetDir = GLINT_ASSET_DIR;
}

std::filesystem::path assetPath(std::string_view relative) { return g_assetDir / relative; }

void setAssetDir(const std::filesystem::path& dir) { g_assetDir = dir; }

}  // namespace glint
