#pragma once

// Move-only owner of a GL buffer. Wraps what 01_triangle did by hand:
//   glCreateBuffers + glNamedBufferStorage (+ glNamedBufferSubData for updates).
// Storage is immutable in size (like a VkBuffer); pass GL_DYNAMIC_STORAGE_BIT to be allowed to update().

#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

#include "gl/gl.h"

namespace glint::gl {

class Buffer {
 public:
  Buffer() = default;
  Buffer(size_t size, const void* data, GLbitfield flags = 0, std::string_view label = {}) : m_size(size) {
    glCreateBuffers(1, &m_id);
    glNamedBufferStorage(m_id, GLsizeiptr(size), data, flags);
    if (!label.empty()) glObjectLabel(GL_BUFFER, m_id, GLsizei(label.size()), label.data());
  }
  template <typename T>
  explicit Buffer(const std::vector<T>& data, GLbitfield flags = 0, std::string_view label = {})
      : Buffer(data.size() * sizeof(T), data.data(), flags, label) {}

  ~Buffer() { glDeleteBuffers(1, &m_id); }
  Buffer(Buffer&& other) noexcept : m_id(std::exchange(other.m_id, 0)), m_size(std::exchange(other.m_size, 0)) {}
  Buffer& operator=(Buffer&& other) noexcept {
    std::swap(m_id, other.m_id);
    std::swap(m_size, other.m_size);
    return *this;
  }
  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;

  GLuint id() const { return m_id; }
  size_t size() const { return m_size; }

  // Needs GL_DYNAMIC_STORAGE_BIT. The driver copies `data` right away (or orphans/renames internally),
  // so it's safe to call while the GPU still reads the old contents. VK: you must avoid that race yourself.
  void update(const void* data, size_t size, size_t offset = 0) {
    glNamedBufferSubData(m_id, GLintptr(offset), GLsizeiptr(size), data);
  }
  template <typename T>
  void update(const T& value, size_t offset = 0) {
    update(&value, sizeof(T), offset);
  }

 private:
  GLuint m_id = 0;
  size_t m_size = 0;
};

}  // namespace glint::gl
