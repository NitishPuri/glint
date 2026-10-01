// 04_basic_shading — OpenGL. An OBJ mesh lit per fragment. New here: a uniform buffer (std140) instead of
// loose uniforms, and a *non-indexed* draw (every triangle corner is its own vertex; 05 fixes that).

#include "../common/shading.h"
#include "core/assets.h"
#include "core/camera.h"
#include "gl/buffer.h"
#include "gl/gl.h"
#include "gl/mesh.h"
#include "gl/shader.h"
#include "gl/technique.h"
#include "gl/texture.h"
#include "params.h"

namespace glint::basic_shading {

class BasicShadingGL final : public gl::Technique {
 public:
  void init() override {
    m_program = gl::Program({"common/shaders/shading.vert", "common/shaders/shading.frag"});
    const MeshData suzanne = loadObj(assetPath("suzanne.obj"));  // flat: 2904 vertices, 968 triangles
    m_vertexCount = GLsizei(suzanne.vertexCount());
    m_mesh = gl::Mesh(suzanne, "suzanne");
    m_texture = gl::Texture(loadImage(assetPath("suzanne.jpg")), true, "suzanne.jpg");
    m_sampler = gl::Sampler(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT, "trilinear repeat");
    // DYNAMIC_STORAGE: we rewrite it every frame with glNamedBufferSubData.
    m_uniforms = gl::Buffer(sizeof(shading::Uniforms), nullptr, GL_DYNAMIC_STORAGE_BIT, "04 uniforms");
  }

  void setupCamera(Camera& camera) override { camera.setHome(kCameraHome); }

  void update(float /*dt*/, Frame& frame) override {
    m_program.reloadIfChanged();
    const shading::Uniforms u =
        shading::makeUniforms(frame.camera.projection(frame.aspect(), ClipDepth::NegOneToOne), frame.camera.view(),
                              glm::mat4(1.0f), m_params.light, m_params.material);
    // GL: just overwrite it. If the GPU is still reading last frame's contents, the driver copies/renames
    // behind our back. VK: the app must not touch memory the GPU may still read -> one buffer per frame in
    // flight.
    m_uniforms.update(u);
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
    // Uniform buffer to binding point 0 (the `binding = 0` in the shader); texture + sampler to unit 0.
    // VK: both become entries of one descriptor set, bound with vkCmdBindDescriptorSets.
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_uniforms.id());
    m_texture.bind(1);  // unit 1: the shared shader's SAMPLER(0, 1) (VK: set 0, binding 1)
    m_sampler.bind(1);

    // Non-indexed: glDrawArrays walks the vertex buffers in order, 3 vertices per triangle.
    m_mesh.vertexArray().bind();
    glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);

    glBindSampler(1, 0);
  }

  void ui() override {
    ImGui::Text("%d vertices, drawn without an index buffer", int(m_vertexCount));
    paramsUi(m_params);
  }

 private:
  Params m_params;
  gl::Program m_program;
  gl::Mesh m_mesh;
  GLsizei m_vertexCount = 0;
  gl::Texture m_texture;
  gl::Sampler m_sampler;
  gl::Buffer m_uniforms;
};

GLINT_REGISTER_GL("04_basic_shading", BasicShadingGL);

}  // namespace glint::basic_shading
