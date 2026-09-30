#include "vk/mesh.h"

#include <stdexcept>

#include "core/log.h"

namespace glint::vk {

Mesh::Mesh(const Context& ctx, const MeshData& data, const std::string& name)
    : m_indexCount(uint32_t(data.indices.size())), m_vertexCount(uint32_t(data.vertexCount())) {
  auto add = [&](const auto& values, uint32_t location, VkFormat format, const char* attribute) {
    if (values.empty()) return;
    const uint32_t stride = uint32_t(sizeof(values[0]));
    m_vertexBuffers.push_back(createDeviceLocalBuffer(ctx, values.data(), values.size() * stride,
                                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, name + " " + attribute));
    m_bindings.push_back(location);
    m_input.bindings.push_back({location, stride, VK_VERTEX_INPUT_RATE_VERTEX});
    m_input.attributes.push_back({location, location, format, 0});
  };
  add(data.positions, kAttribPosition, VK_FORMAT_R32G32B32_SFLOAT, "positions");
  add(data.normals, kAttribNormal, VK_FORMAT_R32G32B32_SFLOAT, "normals");
  add(data.uvs, kAttribUv, VK_FORMAT_R32G32_SFLOAT, "uvs");
  add(data.tangents, kAttribTangent, VK_FORMAT_R32G32B32A32_SFLOAT, "tangents");
  m_indices = createDeviceLocalBuffer(ctx, data.indices.data(), data.indices.size() * sizeof(uint32_t),
                                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT, name + " indices");
}

VertexInput Mesh::vertexInput(std::initializer_list<AttribLocation> locations) const {
  VertexInput subset;
  for (AttribLocation location : locations) {
    bool found = false;
    for (size_t i = 0; i < m_input.bindings.size(); ++i) {
      if (m_input.attributes[i].location != location) continue;
      subset.bindings.push_back(m_input.bindings[i]);
      subset.attributes.push_back(m_input.attributes[i]);
      found = true;
    }
    if (!found) throw std::runtime_error(fmt::format("mesh has no attribute at location {}", uint32_t(location)));
  }
  return subset;
}

VertexInput Mesh::positionsOnly() {
  return {{{kAttribPosition, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX}},
          {{kAttribPosition, kAttribPosition, VK_FORMAT_R32G32B32_SFLOAT, 0}}};
}

void Mesh::draw(VkCommandBuffer cmd) const {
  for (size_t i = 0; i < m_vertexBuffers.size(); ++i) {
    const VkBuffer buffer = m_vertexBuffers[i].handle();
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, m_bindings[i], 1, &buffer, &offset);
  }
  vkCmdBindIndexBuffer(cmd, m_indices.handle(), 0, VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(cmd, m_indexCount, 1, 0, 0, 0);
}

void Mesh::drawPositionsOnly(VkCommandBuffer cmd) const {
  const VkBuffer buffer = m_vertexBuffers.front().handle();  // positions are always added first
  const VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(cmd, kAttribPosition, 1, &buffer, &offset);
  vkCmdBindIndexBuffer(cmd, m_indices.handle(), 0, VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(cmd, m_indexCount, 1, 0, 0, 0);
}

}  // namespace glint::vk
