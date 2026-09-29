// 02_cube — OpenGL. First technique on the gl/ helpers (Program, Mesh = Buffer + VertexArray), which wrap
// the calls 01_triangle spells out. New here: 3D transforms, the depth buffer, back-face culling.

#include "core/assets.h"
#include "core/camera.h"
#include "gl/gl.h"
#include "gl/mesh.h"
#include "gl/shader.h"
#include "gl/technique.h"
#include "params.h"

namespace glint::cube {

class CubeGL final : public gl::Technique {
 public:
  void init() override {
    m_program = gl::Program({"02_cube/shaders/cube.gl.vert", "02_cube/shaders/cube.gl.frag"});
    m_mesh = gl::Mesh(makeCube(), "cube");
  }

  void update(float dt, Frame& /*frame*/) override {
    m_program.reloadIfChanged();
    m_angle += m_params.rotationSpeed * dt;
  }

  void render(const Frame& frame) override {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, frame.framebufferSize.x, frame.framebufferSize.y);
    glClearColor(m_params.clearColor.r, m_params.clearColor.g, m_params.clearColor.b, 1.0f);
    // The default framebuffer came with a depth buffer (GLFW asks for 24 bits by default).
    // VK: you create the depth image, its memory and view yourself, and attach it in VkRenderingInfo.
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // VK: VkPipelineDepthStencilStateCreateInfo + VkPipelineRasterizationStateCreateInfo.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    if (m_params.cullBackFaces) {
      glEnable(GL_CULL_FACE);
      glCullFace(GL_BACK);
      glFrontFace(GL_CCW);
    } else {
      glDisable(GL_CULL_FACE);
    }

    // GL: clip-space z in [-1, 1].
    const glm::mat4 mvp = frame.camera.viewProjection(frame.aspect(), ClipDepth::NegOneToOne) * model(m_angle);
    m_program.use();
    m_program.set(0, mvp);
    m_mesh.draw();
  }

  void ui() override { paramsUi(m_params); }

 private:
  Params m_params;
  float m_angle = 0.0f;
  gl::Program m_program;
  gl::Mesh m_mesh;
};

GLINT_REGISTER_GL("02_cube", CubeGL);

}  // namespace glint::cube
