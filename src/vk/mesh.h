#pragma once

// A MeshData in DEVICE_LOCAL buffers, the VK twin of gl::Mesh: one buffer per attribute (non-interleaved),
// each in its own vertex binding = its attribute location (kAttribPosition, kAttribNormal, ...).
// Difference from GL: the *layout* isn't stored with the buffers (no VAO) — it's baked into each pipeline.
// vertexInput() describes it for vk::GraphicsPipelineDesc, and draw() binds the buffers every time.

#include <initializer_list>
#include <string>
#include <vector>

#include "core/assets.h"
#include "vk/buffer.h"

namespace glint::vk {

struct VertexInput {
  std::vector<VkVertexInputBindingDescription> bindings;
  std::vector<VkVertexInputAttributeDescription> attributes;
};

class Mesh {
 public:
  Mesh() = default;
  Mesh(const Context& ctx, const MeshData& data, const std::string& name = "mesh");

  // All attributes this mesh has.
  const VertexInput& vertexInput() const { return m_input; }
  // Just these attributes (each must exist in the mesh) — declare only what the vertex shader reads.
  VertexInput vertexInput(std::initializer_list<AttribLocation> locations) const;
  static VertexInput positionsOnly();

  // vkCmdBindVertexBuffers (all attributes; bindings the pipeline doesn't use are ignored) +
  // vkCmdBindIndexBuffer + vkCmdDrawIndexed.
  void draw(VkCommandBuffer cmd) const;
  // Non-indexed: vkCmdDraw over all vertices in order (for flat meshes, like GL's glDrawArrays).
  void drawNonIndexed(VkCommandBuffer cmd) const;
  // Same as draw(), binding only the position buffer (for pipelines built with positionsOnly()).
  void drawPositionsOnly(VkCommandBuffer cmd) const;

  uint32_t indexCount() const { return m_indexCount; }
  uint32_t vertexCount() const { return m_vertexCount; }

 private:
  std::vector<Buffer> m_vertexBuffers;  // one per attribute, in binding order
  std::vector<uint32_t> m_bindings;     // binding number of each buffer
  Buffer m_indices;
  VertexInput m_input;
  uint32_t m_indexCount = 0;
  uint32_t m_vertexCount = 0;
};

}  // namespace glint::vk
