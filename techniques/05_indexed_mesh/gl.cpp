// 05_indexed_mesh — OpenGL. Same shading as 04, but the mesh is indexed (shared vertices + index buffer),
// and it's drawn twice with different uniforms. Optional alpha blending.

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

namespace glint::indexed_mesh {

class IndexedMeshGL final : public gl::Technique {
 public:
  void init() override {
    m_program = gl::Program({"common/shaders/shading.vert", "common/shaders/shading.frag"});
    MeshData suzanne = loadObj(assetPath("suzanne.obj"));
    m_flatVertexCount = suzanne.vertexCount();
    indexMesh(suzanne);  // 2904 -> 590 vertices; 2904 indices
    m_mesh = gl::Mesh(suzanne, "suzanne indexed");
    m_vertexCount = suzanne.vertexCount();
    m_texture = gl::Texture(loadImage(assetPath("suzanne.jpg")), true, "suzanne.jpg");
    m_sampler = gl::Sampler(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT, "trilinear repeat");
    m_uniforms = gl::Buffer(sizeof(shading::Uniforms), nullptr, GL_DYNAMIC_STORAGE_BIT, "05 uniforms");
  }

  void setupCamera(Camera& camera) override { camera.setHome(kCameraHome); }

  void update(float /*dt*/, Frame& frame) override {
    m_program.reloadIfChanged();
    m_projection = frame.camera.projection(frame.aspect(), ClipDepth::NegOneToOne);
    m_view = frame.camera.view();
  }

  void render(const Frame& frame) override {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, frame.framebufferSize.x, frame.framebufferSize.y);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    // VK: blending is pipeline state, so "transparent" means a second VkPipeline.
    if (m_params.transparent) {
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
      glDisable(GL_BLEND);
    }

    m_program.use();
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_uniforms.id());
    m_texture.bind(1);  // unit 1: the shared shader's SAMPLER(0, 1) (VK: set 0, binding 1)
    m_sampler.bind(1);

    for (int instance = 0; instance < 2; ++instance) {
      const shading::Uniforms u = shading::makeUniforms(m_projection, m_view, model(instance), m_params.light,
                                                        material(m_params, instance));
      // Rewriting the buffer between two draws that both read it: legal in GL — the driver makes sure the
      // first draw still sees the old contents. VK has no such magic: the command buffer only records
      // *which* buffer to read, and both draws would see whatever is in it when the GPU runs them. So VK
      // needs per-draw data: push constants, or one buffer slot per draw (dynamic offsets).
      m_uniforms.update(u);
      m_mesh.draw();  // glDrawElements: 2904 indices into 590 vertices
    }

    glDisable(GL_BLEND);
    glBindSampler(1, 0);
  }

  void ui() override {
    ImGui::Text("indexing: %zu -> %zu vertices (%.0f%% fewer)", m_flatVertexCount, m_vertexCount,
                100.0 * (1.0 - double(m_vertexCount) / double(m_flatVertexCount)));
    paramsUi(m_params);
  }

 private:
  Params m_params;
  gl::Program m_program;
  gl::Mesh m_mesh;
  size_t m_flatVertexCount = 0;
  size_t m_vertexCount = 0;
  gl::Texture m_texture;
  gl::Sampler m_sampler;
  gl::Buffer m_uniforms;
  glm::mat4 m_projection{1.0f};
  glm::mat4 m_view{1.0f};
};

GLINT_REGISTER_GL("05_indexed_mesh", IndexedMeshGL);

}  // namespace glint::indexed_mesh
