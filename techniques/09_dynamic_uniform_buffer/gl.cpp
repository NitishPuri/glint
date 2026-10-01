// 09_dynamic_uniform_buffer — OpenGL. 125 cubes; every model matrix lives in one uniform buffer, each at an
// offset that is a multiple of GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT. Before each draw, glBindBufferRange
// points binding 1 at that object's slice. (VK: a UNIFORM_BUFFER_DYNAMIC descriptor + per-draw offset.)

#include <cstring>
#include <vector>

#include "core/assets.h"
#include "core/camera.h"
#include "core/log.h"
#include "gl/buffer.h"
#include "gl/gl.h"
#include "gl/mesh.h"
#include "gl/shader.h"
#include "gl/technique.h"
#include "params.h"

namespace glint::dynamic_ubo {

class DynamicUboGL final : public gl::Technique {
 public:
  void setupCamera(Camera& camera) override { camera.setHome(kCameraHome); }

  void init() override {
    m_program = gl::Program({"09_dynamic_uniform_buffer/shaders/objects.gl.vert",
                             "09_dynamic_uniform_buffer/shaders/objects.gl.frag"});
    m_mesh = gl::Mesh(makeCube(), "cube");

    // Offsets passed to glBindBufferRange must be multiples of this (radeonsi here: 4; NVIDIA: 256 typically).
    GLint alignment = 0;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment);
    m_alignment = size_t(alignment);
    m_stride = alignUp(sizeof(glm::mat4), m_alignment);
    log::info("09 GL: GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT = {}, stride {}", alignment, m_stride);

    m_cameraUbo = gl::Buffer(sizeof(glm::mat4), nullptr, GL_DYNAMIC_STORAGE_BIT, "09 camera");
    m_objectUbo = gl::Buffer(m_stride * kObjects, nullptr, GL_DYNAMIC_STORAGE_BIT, "09 objects");
    m_cpuCopy.resize(m_stride * kObjects);
  }

  void update(float dt, Frame& frame) override {
    m_program.reloadIfChanged();
    m_objects.animate(dt, m_params);
    m_cameraUbo.update(frame.camera.viewProjection(frame.aspect(), ClipDepth::NegOneToOne));
    // All 125 matrices, each at i * stride (the bytes in between are padding), in one upload.
    for (int i = 0; i < kObjects; ++i) {
      const glm::mat4 model = m_objects.model(i);
      std::memcpy(m_cpuCopy.data() + size_t(i) * m_stride, &model, sizeof(model));
    }
    m_objectUbo.update(m_cpuCopy.data(), m_cpuCopy.size());
  }

  void render(const Frame& frame) override {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, frame.framebufferSize.x, frame.framebufferSize.y);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);

    m_program.use();
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_cameraUbo.id());
    for (int i = 0; i < kObjects; ++i) {
      // Binding point 1 = bytes [i * stride, i * stride + 64) of the object buffer.
      glBindBufferRange(GL_UNIFORM_BUFFER, 1, m_objectUbo.id(), GLintptr(size_t(i) * m_stride), sizeof(glm::mat4));
      m_mesh.draw();
    }
  }

  void ui() override {
    ImGui::Text("%d objects, 1 buffer: alignment %zu, stride %zu bytes", kObjects, m_alignment, m_stride);
    paramsUi(m_params);
  }

 private:
  Params m_params;
  Objects m_objects;
  gl::Program m_program;
  gl::Mesh m_mesh;
  gl::Buffer m_cameraUbo, m_objectUbo;
  std::vector<unsigned char> m_cpuCopy;
  size_t m_alignment = 0, m_stride = 0;
};

GLINT_REGISTER_GL("09_dynamic_uniform_buffer", DynamicUboGL);

}  // namespace glint::dynamic_ubo
