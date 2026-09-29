// 01_triangle — OpenGL, written raw: every GL object is created and used right here, no helpers.
// (From 02 on, gl/shader.h, gl/buffer.h and gl/vertex_array.h wrap exactly the calls you see below.)
//
// Uses DSA ("direct state access", GL 4.5): functions take the object name as an argument
// (glNamedBufferStorage(buf, ...)) instead of editing whatever is bound (glBindBuffer + glBufferData).
// It's the GL style closest to VK, where every call names the object it acts on.

#include <cstddef>
#include <stdexcept>
#include <string>

#include "core/assets.h"
#include "core/log.h"
#include "gl/gl.h"
#include "gl/technique.h"
#include "params.h"

namespace glint::triangle {

namespace {

// Compiles one stage; throws with the driver's error log on failure.
// GL: the driver compiles GLSL text at runtime. VK: glslc compiled it to SPIR-V at build time.
GLuint compileStage(GLenum stage, const std::string& path) {
  const std::string source = readTextFile(path);
  const char* text = source.c_str();

  GLuint shader = glCreateShader(stage);
  glShaderSource(shader, 1, &text, nullptr);
  glCompileShader(shader);

  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char info[2048];
    glGetShaderInfoLog(shader, sizeof(info), nullptr, info);
    glDeleteShader(shader);
    throw std::runtime_error(fmt::format("{}:\n{}", path, info));
  }
  return shader;
}

}  // namespace

class TriangleGL final : public gl::Technique {
 public:
  void init() override {
    // --- program: vertex + fragment stage linked together -------------------------------------------
    // VK: two VkShaderModules, plugged into a VkGraphicsPipeline together with *all* the fixed-function
    // state (vertex layout, topology, blend, depth...). A GL program holds only the shaders.
    const std::string dir = std::string(GLINT_SHADER_DIR) + "/01_triangle/shaders/";
    const GLuint vs = compileStage(GL_VERTEX_SHADER, dir + "triangle.gl.vert");
    const GLuint fs = compileStage(GL_FRAGMENT_SHADER, dir + "triangle.gl.frag");
    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);
    glDeleteShader(vs);  // only flagged for deletion; the program keeps them alive while attached
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok) {
      char info[2048];
      glGetProgramInfoLog(m_program, sizeof(info), nullptr, info);
      throw std::runtime_error(fmt::format("link failed:\n{}", info));
    }

    // --- buffers ------------------------------------------------------------------------------------
    // glNamedBufferStorage makes an *immutable-size* buffer and uploads the data in one call. Flags = 0:
    // the CPU never touches it again, so the driver is free to put it in VRAM.
    // VK: vkCreateBuffer + vkGetBufferMemoryRequirements + pick a memory type + vkAllocateMemory +
    // vkBindBufferMemory, then a staging buffer and a copy command to get the data into VRAM.
    glCreateBuffers(1, &m_vertexBuffer);
    glNamedBufferStorage(m_vertexBuffer, sizeof(kVertices), kVertices, 0);
    // (Pre-DSA: glGenBuffers; glBindBuffer(GL_ARRAY_BUFFER, b); glBufferData(GL_ARRAY_BUFFER, ...).)

    // One index buffer holding both index lists back to back; the draw picks which range to use.
    glCreateBuffers(1, &m_indexBuffer);
    glNamedBufferStorage(m_indexBuffer, sizeof(kTriangleIndices) + sizeof(kQuadIndices), nullptr,
                         GL_DYNAMIC_STORAGE_BIT);  // DYNAMIC_STORAGE: allows the two SubData uploads below
    glNamedBufferSubData(m_indexBuffer, 0, sizeof(kTriangleIndices), kTriangleIndices);
    glNamedBufferSubData(m_indexBuffer, sizeof(kTriangleIndices), sizeof(kQuadIndices), kQuadIndices);

    // --- vertex array: how to pull attributes out of the buffers --------------------------------------
    // DSA splits it in two, just like VK's VkPipelineVertexInputStateCreateInfo:
    //   binding 0 = "a buffer with stride sizeof(Vertex)"           (VkVertexInputBindingDescription)
    //   location N = "format + offset within a vertex of binding 0" (VkVertexInputAttributeDescription)
    // Difference: the GL VAO also stores *which* buffer is bound; in VK the layout is part of the pipeline
    // and the buffer is bound separately at draw time (vkCmdBindVertexBuffers).
    glCreateVertexArrays(1, &m_vao);
    glVertexArrayVertexBuffer(m_vao, /*binding*/ 0, m_vertexBuffer, /*offset*/ 0, sizeof(Vertex));

    glEnableVertexArrayAttrib(m_vao, kAttribPosition);
    glVertexArrayAttribFormat(m_vao, kAttribPosition, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
    glVertexArrayAttribBinding(m_vao, kAttribPosition, 0);

    glEnableVertexArrayAttrib(m_vao, kAttribColor);
    glVertexArrayAttribFormat(m_vao, kAttribColor, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, color));
    glVertexArrayAttribBinding(m_vao, kAttribColor, 0);

    glVertexArrayElementBuffer(m_vao, m_indexBuffer);
    // (Pre-DSA: glBindVertexArray + glBindBuffer + glVertexAttribPointer + glEnableVertexAttribArray.)

    // Names in RenderDoc / KHR_debug messages. VK: vkSetDebugUtilsObjectNameEXT.
    glObjectLabel(GL_PROGRAM, m_program, -1, "01 triangle program");
    glObjectLabel(GL_BUFFER, m_vertexBuffer, -1, "01 vertices");
    glObjectLabel(GL_BUFFER, m_indexBuffer, -1, "01 indices");
    glObjectLabel(GL_VERTEX_ARRAY, m_vao, -1, "01 vao");
  }

  ~TriangleGL() override {
    // Deleting name 0 is a no-op, so this is safe even if init() threw halfway.
    // VK: vkDestroy* each object, and only once the GPU is done with it (vkDeviceWaitIdle or fences).
    // GL defers the actual free until the GPU is done by itself.
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_indexBuffer);
    glDeleteBuffers(1, &m_vertexBuffer);
    glDeleteProgram(m_program);
  }

  void update(float dt, Frame& /*frame*/) override { m_angle += m_params.rotationSpeed * dt; }

  void render(const Frame& frame) override {
    // Target: the default framebuffer (the window's back buffer), whole window.
    // VK: vkCmdBeginRendering with the acquired swapchain image + vkCmdSetViewport/Scissor.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, frame.framebufferSize.x, frame.framebufferSize.y);
    glClearColor(m_params.clearColor.r, m_params.clearColor.g, m_params.clearColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);  // VK: loadOp = CLEAR on the color attachment

    // Fixed-function state this draw relies on. VK: baked into the pipeline at creation.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glUseProgram(m_program);
    const glm::mat4 xform = transform(m_angle, frame.aspect());
    glProgramUniformMatrix4fv(m_program, /*location*/ 0, 1, GL_FALSE, &xform[0][0]);  // VK: vkCmdPushConstants
    glBindVertexArray(m_vao);  // VK: vkCmdBindPipeline + vkCmdBindVertexBuffers + vkCmdBindIndexBuffer

    if (m_params.quad) {
      glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, reinterpret_cast<const void*>(sizeof(kTriangleIndices)));
    } else {
      glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, nullptr);
    }
    // VK: vkCmdDrawIndexed(cmd, indexCount, 1, firstIndex, 0, 0) — firstIndex is in indices, not bytes.
  }

  void ui() override { paramsUi(m_params); }

 private:
  Params m_params;
  float m_angle = 0.0f;
  GLuint m_program = 0;
  GLuint m_vertexBuffer = 0;
  GLuint m_indexBuffer = 0;
  GLuint m_vao = 0;
};

GLINT_REGISTER_GL("01_triangle", TriangleGL);

}  // namespace glint::triangle
