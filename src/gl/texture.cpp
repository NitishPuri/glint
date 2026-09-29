#include "gl/texture.h"

#include <algorithm>

namespace glint::gl {

namespace {

void setLabel(GLenum type, GLuint id, std::string_view label) {
  if (!label.empty()) glObjectLabel(type, id, GLsizei(label.size()), label.data());
}

}  // namespace

Texture::Texture(GLenum internalFormat, int width, int height, int levels, std::string_view label)
    : m_width(width), m_height(height) {
  glCreateTextures(GL_TEXTURE_2D, 1, &m_id);
  glTextureStorage2D(m_id, levels, internalFormat, width, height);
  setLabel(GL_TEXTURE, m_id, label);
}

Texture::Texture(const ImageData& image, bool mipmaps, std::string_view label)
    : m_width(image.width), m_height(image.height) {
  int levels = 1;
  if (mipmaps) {
    for (int size = std::max(image.width, image.height); size > 1; size /= 2) ++levels;
  }
  glCreateTextures(GL_TEXTURE_2D, 1, &m_id);
  glTextureStorage2D(m_id, levels, GL_RGBA8, image.width, image.height);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTextureSubImage2D(m_id, 0, 0, 0, image.width, image.height, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
  if (levels > 1) glGenerateTextureMipmap(m_id);
  setLabel(GL_TEXTURE, m_id, label);
}

Sampler::Sampler(GLenum minFilter, GLenum magFilter, GLenum wrap, std::string_view label) {
  glCreateSamplers(1, &m_id);
  glSamplerParameteri(m_id, GL_TEXTURE_MIN_FILTER, GLint(minFilter));
  glSamplerParameteri(m_id, GL_TEXTURE_MAG_FILTER, GLint(magFilter));
  glSamplerParameteri(m_id, GL_TEXTURE_WRAP_S, GLint(wrap));
  glSamplerParameteri(m_id, GL_TEXTURE_WRAP_T, GLint(wrap));
  setLabel(GL_SAMPLER, m_id, label);
}

}  // namespace glint::gl
