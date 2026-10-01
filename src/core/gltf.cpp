#include "core/gltf.h"

#include <stb_image.h>

#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>
#include <unordered_set>

#include "core/log.h"
#include "core/timer.h"

// tinygltf would otherwise compile its own copy of stb_image; ours is already built in assets.cpp.
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace glint {

namespace {

// tinygltf hands every image's *encoded* bytes (PNG/JPEG) to this callback. We just keep them, and decode
// later only the images a material actually uses (FlightHelmet's unused occlusion/roughness maps alone would
// be ~100 MB of RGBA).
bool keepEncodedImage(tinygltf::Image* image, const int, std::string*, std::string*, int, int,
                      const unsigned char* bytes, int size, void*) {
  image->image.assign(bytes, bytes + size);
  image->width = -1;  // marks "still encoded"
  return true;
}

// Reads accessor `index` as `count` elements of `components` floats (VEC2/VEC3/VEC4 of FLOAT).
template <typename Vec>
std::vector<Vec> readFloats(const tinygltf::Model& model, int index) {
  const tinygltf::Accessor& accessor = model.accessors[size_t(index)];
  if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) throw std::runtime_error("gltf: expected float data");
  const tinygltf::BufferView& view = model.bufferViews[size_t(accessor.bufferView)];
  const unsigned char* base = model.buffers[size_t(view.buffer)].data.data() + view.byteOffset + accessor.byteOffset;
  const size_t stride = view.byteStride ? view.byteStride : sizeof(Vec);  // 0 = tightly packed
  std::vector<Vec> out(accessor.count);
  for (size_t i = 0; i < accessor.count; ++i) std::memcpy(&out[i], base + i * stride, sizeof(Vec));
  return out;
}

std::vector<uint32_t> readIndices(const tinygltf::Model& model, int index) {
  const tinygltf::Accessor& accessor = model.accessors[size_t(index)];
  const tinygltf::BufferView& view = model.bufferViews[size_t(accessor.bufferView)];
  const unsigned char* base = model.buffers[size_t(view.buffer)].data.data() + view.byteOffset + accessor.byteOffset;
  std::vector<uint32_t> out(accessor.count);
  // Widened to 32 bits: one index type for every mesh keeps both backends simple (FlightHelmet uses 16).
  for (size_t i = 0; i < accessor.count; ++i) {
    switch (accessor.componentType) {
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: out[i] = base[i]; break;
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: out[i] = reinterpret_cast<const uint16_t*>(base)[i]; break;
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: out[i] = reinterpret_cast<const uint32_t*>(base)[i]; break;
      default: throw std::runtime_error("gltf: unsupported index type");
    }
  }
  return out;
}

glm::mat4 localMatrix(const tinygltf::Node& node) {
  if (node.matrix.size() == 16) return glm::mat4(glm::make_mat4(node.matrix.data()));  // column-major, like glm
  glm::mat4 m(1.0f);
  if (node.translation.size() == 3) m = glm::translate(m, glm::vec3(glm::make_vec3(node.translation.data())));
  if (node.rotation.size() == 4) {  // glTF quaternion order: x, y, z, w
    m *= glm::mat4_cast(glm::quat(float(node.rotation[3]), float(node.rotation[0]), float(node.rotation[1]),
                                  float(node.rotation[2])));
  }
  if (node.scale.size() == 3) m = glm::scale(m, glm::vec3(glm::make_vec3(node.scale.data())));
  return m;
}

}  // namespace

size_t ModelData::triangleCount() const {
  size_t n = 0;
  for (const GltfDraw& d : draws) n += primitives[size_t(d.primitive)].mesh.triangleCount();
  return n;
}

ModelData loadGltf(const std::filesystem::path& path) {
  ScopedTimer timer(fmt::format("loadGltf {}", path.filename().string()));
  tinygltf::Model model;
  tinygltf::TinyGLTF loader;
  loader.SetImageLoader(keepEncodedImage, nullptr);
  std::string error, warning;
  const bool binary = path.extension() == ".glb";
  const bool ok = binary ? loader.LoadBinaryFromFile(&model, &error, &warning, path.string())
                         : loader.LoadASCIIFromFile(&model, &error, &warning, path.string());
  if (!warning.empty()) log::debug("loadGltf {}: {}", path.filename().string(), warning);
  if (!ok) throw std::runtime_error(fmt::format("loadGltf '{}': {}", path.string(), error));

  ModelData out;

  // --- materials ---------------------------------------------------------------------------------------
  auto textureImage = [&](int textureIndex) {
    return textureIndex < 0 ? -1 : model.textures[size_t(textureIndex)].source;
  };
  for (const tinygltf::Material& m : model.materials) {
    GltfMaterial mat;
    mat.name = m.name;
    const auto& f = m.pbrMetallicRoughness.baseColorFactor;
    mat.baseColorFactor = glm::vec4(float(f[0]), float(f[1]), float(f[2]), float(f[3]));
    mat.baseColorImage = textureImage(m.pbrMetallicRoughness.baseColorTexture.index);
    mat.normalImage = textureImage(m.normalTexture.index);
    mat.doubleSided = m.doubleSided;
    mat.alphaMask = m.alphaMode == "MASK";
    mat.alphaCutoff = float(m.alphaCutoff);
    if (m.alphaMode == "BLEND") log::warn("loadGltf: material '{}' uses BLEND; drawn opaque", m.name);
    out.materials.push_back(mat);
  }

  // --- images: decode only the referenced ones ---------------------------------------------------------
  std::unordered_set<int> used;
  for (const GltfMaterial& m : out.materials) {
    if (m.baseColorImage >= 0) used.insert(m.baseColorImage);
    if (m.normalImage >= 0) used.insert(m.normalImage);
  }
  out.images.resize(model.images.size());
  for (int i : used) {
    const std::vector<unsigned char>& encoded = model.images[size_t(i)].image;
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(encoded.data(), int(encoded.size()), &w, &h, &channels, 4);
    if (!pixels) throw std::runtime_error(fmt::format("loadGltf: image {} '{}': {}", i, model.images[size_t(i)].uri,
                                                      stbi_failure_reason()));
    ImageData& img = out.images[size_t(i)];
    img.width = w;
    img.height = h;
    img.channels = 4;
    img.pixels.assign(pixels, pixels + size_t(w) * h * 4);  // no flip: glTF uv (0,0) = top-left = first row
    stbi_image_free(pixels);
  }

  // --- meshes: one GltfPrimitive per glTF primitive --------------------------------------------------------
  std::vector<std::vector<int>> meshPrimitives(model.meshes.size());  // glTF mesh -> our primitive indices
  for (size_t m = 0; m < model.meshes.size(); ++m) {
    for (const tinygltf::Primitive& p : model.meshes[m].primitives) {
      if (p.mode != TINYGLTF_MODE_TRIANGLES && p.mode != -1) {
        log::warn("loadGltf: skipping non-triangle primitive in mesh '{}'", model.meshes[m].name);
        continue;
      }
      GltfPrimitive prim;
      prim.material = p.material;
      auto attribute = [&](const char* name) {
        auto it = p.attributes.find(name);
        return it == p.attributes.end() ? -1 : it->second;
      };
      prim.mesh.positions = readFloats<glm::vec3>(model, attribute("POSITION"));
      if (attribute("NORMAL") >= 0) prim.mesh.normals = readFloats<glm::vec3>(model, attribute("NORMAL"));
      if (attribute("TEXCOORD_0") >= 0) prim.mesh.uvs = readFloats<glm::vec2>(model, attribute("TEXCOORD_0"));
      // glTF tangents use our convention exactly: xyz + w = handedness, bitangent = cross(N, T) * w.
      if (attribute("TANGENT") >= 0) prim.mesh.tangents = readFloats<glm::vec4>(model, attribute("TANGENT"));
      if (p.indices >= 0) {
        prim.mesh.indices = readIndices(model, p.indices);
      } else {
        for (uint32_t i = 0; i < prim.mesh.positions.size(); ++i) prim.mesh.indices.push_back(i);
      }
      meshPrimitives[m].push_back(int(out.primitives.size()));
      out.primitives.push_back(std::move(prim));
    }
  }

  // --- nodes: walk the default scene, multiplying transforms down the hierarchy ----------------------------
  out.boundsMin = glm::vec3(std::numeric_limits<float>::max());
  out.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
  std::function<void(int, const glm::mat4&)> visit = [&](int nodeIndex, const glm::mat4& parent) {
    const tinygltf::Node& node = model.nodes[size_t(nodeIndex)];
    const glm::mat4 world = parent * localMatrix(node);
    if (node.mesh >= 0) {
      for (int prim : meshPrimitives[size_t(node.mesh)]) {
        out.draws.push_back({prim, world});
        for (const glm::vec3& p : out.primitives[size_t(prim)].mesh.positions) {
          const glm::vec3 w = glm::vec3(world * glm::vec4(p, 1.0f));
          out.boundsMin = glm::min(out.boundsMin, w);
          out.boundsMax = glm::max(out.boundsMax, w);
        }
      }
    }
    for (int child : node.children) visit(child, world);
  };
  const int scene = model.defaultScene >= 0 ? model.defaultScene : 0;
  for (int root : model.scenes[size_t(scene)].nodes) visit(root, glm::mat4(1.0f));

  log::info("loadGltf {}: {} primitives, {} materials, {} images decoded, {} draws, {} triangles",
            path.filename().string(), out.primitives.size(), out.materials.size(), used.size(), out.draws.size(),
            out.triangleCount());
  return out;
}

}  // namespace glint
