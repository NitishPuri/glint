#pragma once

// Move-only 2D texture and sampler objects. Wraps what 03_textured_cube does raw:
//   glCreateTextures + glTextureStorage2D (immutable) + glTextureSubImage2D + glGenerateTextureMipmap,
//   glCreateSamplers + glSamplerParameteri.
// Texture = the image data (VK: VkImage + memory + VkImageView); Sampler = how it's read (VK: VkSampler).

#include <string_view>
#include <utility>

#include "core/assets.h"
#include "gl/gl.h"

namespace glint::gl {

class Texture {
 public:
  Texture() = default;
  // Empty texture, e.g. a render target: GL_RGBA8, GL_DEPTH_COMPONENT32F, ...
  Texture(GLenum internalFormat, int width, int height, int levels = 1, std::string_view label = {});
  // RGBA8 texture from an image, with a full mip chain if `mipmaps`.
  explicit Texture(const ImageData& image, bool mipmaps = true, std::string_view label = {});

  ~Texture() { glDeleteTextures(1, &m_id); }
  Texture(Texture&& other) noexcept { swap(other); }
  Texture& operator=(Texture&& other) noexcept {
    swap(other);
    return *this;
  }
  Texture(const Texture&) = delete;
  Texture& operator=(const Texture&) = delete;

  GLuint id() const { return m_id; }
  int width() const { return m_width; }
  int height() const { return m_height; }
  explicit operator bool() const { return m_id != 0; }

  void bind(GLuint unit) const { glBindTextureUnit(unit, m_id); }

 private:
  void swap(Texture& other) noexcept {
    std::swap(m_id, other.m_id);
    std::swap(m_width, other.m_width);
    std::swap(m_height, other.m_height);
  }

  GLuint m_id = 0;
  int m_width = 0;
  int m_height = 0;
};

class Sampler {
 public:
  Sampler() = default;
  Sampler(GLenum minFilter, GLenum magFilter, GLenum wrap, std::string_view label = {});

  ~Sampler() { glDeleteSamplers(1, &m_id); }
  Sampler(Sampler&& other) noexcept : m_id(std::exchange(other.m_id, 0)) {}
  Sampler& operator=(Sampler&& other) noexcept {
    std::swap(m_id, other.m_id);
    return *this;
  }
  Sampler(const Sampler&) = delete;
  Sampler& operator=(const Sampler&) = delete;

  GLuint id() const { return m_id; }
  void set(GLenum parameter, GLint value) const { glSamplerParameteri(m_id, parameter, value); }
  void bind(GLuint unit) const { glBindSampler(unit, m_id); }

 private:
  GLuint m_id = 0;
};

}  // namespace glint::gl
