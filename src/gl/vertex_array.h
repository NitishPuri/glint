#pragma once

// Move-only owner of a VAO, set up once with DSA (see 01_triangle for the raw calls):
//   vertexBuffer(binding, ...)  ~ VkVertexInputBindingDescription + vkCmdBindVertexBuffers
//   attribute(location, ...)    ~ VkVertexInputAttributeDescription

#include <string_view>
#include <utility>

#include "gl/buffer.h"
#include "gl/gl.h"

namespace glint::gl {

class VertexArray {
 public:
  explicit VertexArray(std::string_view label = {}) {
    glCreateVertexArrays(1, &m_id);
    if (!label.empty()) glObjectLabel(GL_VERTEX_ARRAY, m_id, GLsizei(label.size()), label.data());
  }
  ~VertexArray() { glDeleteVertexArrays(1, &m_id); }
  VertexArray(VertexArray&& other) noexcept : m_id(std::exchange(other.m_id, 0)) {}
  VertexArray& operator=(VertexArray&& other) noexcept {
    std::swap(m_id, other.m_id);
    return *this;
  }
  VertexArray(const VertexArray&) = delete;
  VertexArray& operator=(const VertexArray&) = delete;

  GLuint id() const { return m_id; }

  // Binding slot `binding` reads vertices from `buffer`, `stride` bytes apart.
  void vertexBuffer(GLuint binding, const Buffer& buffer, GLsizei stride, GLintptr offset = 0) {
    glVertexArrayVertexBuffer(m_id, binding, buffer.id(), offset, stride);
  }

  // Shader input `location` = `components` x `type` at `relativeOffset` inside each vertex of `binding`.
  // Float formats only (glVertexArrayAttribIFormat would be needed for integer inputs).
  void attribute(GLuint location, GLuint binding, GLint components, GLenum type = GL_FLOAT,
                 GLuint relativeOffset = 0) {
    glEnableVertexArrayAttrib(m_id, location);
    glVertexArrayAttribFormat(m_id, location, components, type, GL_FALSE, relativeOffset);
    glVertexArrayAttribBinding(m_id, location, binding);
  }

  void indexBuffer(const Buffer& buffer) { glVertexArrayElementBuffer(m_id, buffer.id()); }

  void bind() const { glBindVertexArray(m_id); }

 private:
  GLuint m_id = 0;
};

}  // namespace glint::gl
