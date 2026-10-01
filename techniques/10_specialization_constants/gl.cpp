// 10_specialization_constants — OpenGL. Three programs from one GLSL source, each compiled with different
// #defines injected after #version (gl::Program's `defines`). The preprocessor strips the unused lighting
// models before compilation. VK does this with specialization constants on one SPIR-V module.

#include <array>

#include "core/assets.h"
#include "core/camera.h"
#include "core/log.h"
#include "gl/buffer.h"
#include "gl/gl.h"
#include "gl/mesh.h"
#include "gl/shader.h"
#include "gl/technique.h"
#include "gl/texture.h"
#include "params.h"

namespace glint::uber {

class UberGL final : public gl::Technique {
 public:
  void setupCamera(Camera& camera) override { camera.setHome(kCameraHome); }

  void init() override {
    for (int i = 0; i < kModels; ++i) buildProgram(i);
    MeshData suzanne = loadObj(assetPath("suzanne.obj"));
    indexMesh(suzanne);
    m_mesh = gl::Mesh(suzanne, "suzanne");
    m_texture = gl::Texture(loadImage(assetPath("suzanne.jpg")), true, "suzanne.jpg");
    m_sampler = gl::Sampler(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT, "trilinear repeat");
    m_ubo = gl::Buffer(sizeof(Uniforms), nullptr, GL_DYNAMIC_STORAGE_BIT, "10 ubo");
  }

  void update(float dt, Frame& frame) override {
    for (gl::Program& program : m_programs) program.reloadIfChanged();
    m_angle += m_params.rotationSpeed * dt;
    drawLabels(frame.framebufferSize);
    // One third of the window per model: aspect = (width / 3) / height.
    const float aspect = frame.aspect() / float(kModels);
    m_ubo.update(makeUniforms(frame.camera.projection(aspect, ClipDepth::NegOneToOne), frame.camera.view(), m_angle,
                              m_params));
  }

  void render(const Frame& frame) override {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, frame.framebufferSize.x, frame.framebufferSize.y);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_ubo.id());
    m_texture.bind(0);
    m_sampler.bind(0);

    const int third = frame.framebufferSize.x / kModels;
    for (int i = 0; i < kModels; ++i) {
      glViewport(third * i, 0, third, frame.framebufferSize.y);
      m_programs[size_t(i)].use();
      m_mesh.draw();
    }
    glBindSampler(0, 0);
  }

  void ui() override {
    if (paramsUi(m_params)) buildProgram(1);  // the toon program bakes in TOON_DESATURATION: recompile it
  }

 private:
  void buildProgram(int model) {
    // Everything after the #version line of each stage is compiled with these in front.
    const std::string defines =
        fmt::format("#define LIGHTING_MODEL {}\n#define TOON_DESATURATION {:.6f}\n", model, m_params.toonDesaturation);
    m_programs[size_t(model)] = gl::Program(
        {"10_specialization_constants/shaders/uber.gl.vert", "10_specialization_constants/shaders/uber.gl.frag"},
        defines);
  }

  Params m_params;
  float m_angle = 0.0f;
  std::array<gl::Program, kModels> m_programs;
  gl::Mesh m_mesh;
  gl::Texture m_texture;
  gl::Sampler m_sampler;
  gl::Buffer m_ubo;
};

GLINT_REGISTER_GL("10_specialization_constants", UberGL);

}  // namespace glint::uber
