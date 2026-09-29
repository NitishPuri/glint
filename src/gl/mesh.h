#pragma once

// A MeshData uploaded to GL: one buffer per attribute (non-interleaved, like MeshData itself), each in
// its own binding slot = its attribute location (core/assets.h: kAttribPosition, kAttribNormal, ...).
// Attributes the MeshData doesn't have are simply not enabled; the shader then reads (0,0,0,1).

#include <string>

#include "core/assets.h"
#include "gl/buffer.h"
#include "gl/vertex_array.h"

namespace glint::gl {

class Mesh {
 public:
  Mesh() = default;
  explicit Mesh(const MeshData& data, const std::string& label = "mesh");

  // Binds the VAO and issues glDrawElements for the whole mesh.
  void draw() const;

  GLsizei indexCount() const { return m_indexCount; }
  const VertexArray& vertexArray() const { return m_vao; }

 private:
  Buffer m_positions, m_normals, m_uvs, m_tangents, m_indices;
  VertexArray m_vao;
  GLsizei m_indexCount = 0;
};

}  // namespace glint::gl
