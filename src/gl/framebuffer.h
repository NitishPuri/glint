#pragma once

// Move-only framebuffer object. Wraps what 07_render_to_texture does raw:
//   glCreateFramebuffers + glNamedFramebufferTexture + glNamedFramebufferDrawBuffers + completeness check.
// VK 1.3 has no framebuffer object at all: attachments are listed in VkRenderingInfo at vkCmdBeginRendering.

#include <stdexcept>
#include <string_view>
#include <utility>

#include "core/log.h"
#include "gl/gl.h"
#include "gl/texture.h"

namespace glint::gl {

class Framebuffer {
 public:
  Framebuffer() = default;
  explicit Framebuffer(std::string_view label) {
    glCreateFramebuffers(1, &m_id);
    if (!label.empty()) glObjectLabel(GL_FRAMEBUFFER, m_id, GLsizei(label.size()), label.data());
  }
  ~Framebuffer() { glDeleteFramebuffers(1, &m_id); }
  Framebuffer(Framebuffer&& other) noexcept : m_id(std::exchange(other.m_id, 0)) {}
  Framebuffer& operator=(Framebuffer&& other) noexcept {
    std::swap(m_id, other.m_id);
    return *this;
  }
  Framebuffer(const Framebuffer&) = delete;
  Framebuffer& operator=(const Framebuffer&) = delete;

  GLuint id() const { return m_id; }

  // Color attachment `index` and fragment output `index` -> that attachment.
  void attachColor(const Texture& texture, GLuint index = 0) {
    glNamedFramebufferTexture(m_id, GL_COLOR_ATTACHMENT0 + index, texture.id(), 0);
    const GLenum buffer = GL_COLOR_ATTACHMENT0 + index;
    glNamedFramebufferDrawBuffers(m_id, 1, &buffer);
  }

  void attachDepth(const Texture& texture) { glNamedFramebufferTexture(m_id, GL_DEPTH_ATTACHMENT, texture.id(), 0); }

  // Depth-only framebuffer: no color buffer to draw to or read from.
  void noColor() {
    glNamedFramebufferDrawBuffer(m_id, GL_NONE);
    glNamedFramebufferReadBuffer(m_id, GL_NONE);
  }

  // Throws if the attachments don't form a usable combination.
  void checkComplete() const {
    const GLenum status = glCheckNamedFramebufferStatus(m_id, GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
      throw std::runtime_error(fmt::format("framebuffer {} incomplete: 0x{:x}", m_id, status));
    }
  }

  void bind() const { glBindFramebuffer(GL_FRAMEBUFFER, m_id); }

 private:
  GLuint m_id = 0;
};

}  // namespace glint::gl
