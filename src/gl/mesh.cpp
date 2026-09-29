#include "gl/mesh.h"

namespace glint::gl {

Mesh::Mesh(const MeshData& data, const std::string& label)
    : m_vao(label + " vao"), m_indexCount(GLsizei(data.indices.size())) {
  auto add = [&](Buffer& buffer, const auto& values, GLuint location, GLint components, const char* name) {
    if (values.empty()) return;
    buffer = Buffer(values, 0, label + " " + name);
    m_vao.vertexBuffer(location, buffer, GLsizei(sizeof(values[0])));
    m_vao.attribute(location, /*binding*/ location, components);
  };
  add(m_positions, data.positions, kAttribPosition, 3, "positions");
  add(m_normals, data.normals, kAttribNormal, 3, "normals");
  add(m_uvs, data.uvs, kAttribUv, 2, "uvs");
  add(m_tangents, data.tangents, kAttribTangent, 4, "tangents");

  m_indices = Buffer(data.indices, 0, label + " indices");
  m_vao.indexBuffer(m_indices);
}

void Mesh::draw() const {
  m_vao.bind();
  glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
}

}  // namespace glint::gl
