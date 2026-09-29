// 06_normal_mapping — OpenGL. Per-vertex tangents (computed on the CPU in core/assets), three textures,
// lighting in tangent space.

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

namespace glint::normal_mapping {

class NormalMappingGL final : public gl::Technique {
 public:
  void init() override {
    m_program = gl::Program({"06_normal_mapping/shaders/normal_mapping.gl.vert",
                             "06_normal_mapping/shaders/normal_mapping.gl.frag"});
    MeshData cylinder = loadObj(assetPath("cylinder/cylinder.obj"));
    indexMesh(cylinder);        // 192 -> 66 vertices
    computeTangents(cylinder);  // after indexing, so shared vertices average their tangents
    m_mesh = gl::Mesh(cylinder, "cylinder");  // positions, normals, uvs, tangents -> locations 0..3

    m_diffuse = gl::Texture(loadImage(assetPath("cylinder/diffuse.jpg")), true, "cylinder diffuse");
    m_normal = gl::Texture(loadImage(assetPath("cylinder/normal.jpg")), true, "cylinder normal");
    m_specular = gl::Texture(loadImage(assetPath("cylinder/specular.jpg")), true, "cylinder specular");
    m_sampler = gl::Sampler(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT, "trilinear repeat");
    m_uniforms = gl::Buffer(sizeof(shading::Uniforms), nullptr, GL_DYNAMIC_STORAGE_BIT, "06 uniforms");
  }

  void setupCamera(Camera& camera) override { camera.setHome(kCameraHome); }

  void update(float /*dt*/, Frame& frame) override {
    m_program.reloadIfChanged();
    shading::Uniforms u =
        shading::makeUniforms(frame.camera.projection(frame.aspect(), ClipDepth::NegOneToOne), frame.camera.view(),
                              glm::mat4(1.0f), m_params.light, m_params.material);
    u.material.w = m_params.useNormalMap ? 1.0f : 0.0f;
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
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_uniforms.id());
    // One sampler object serves all three units.
    m_diffuse.bind(0);
    m_normal.bind(1);
    m_specular.bind(2);
    for (GLuint unit = 0; unit < 3; ++unit) m_sampler.bind(unit);

    m_mesh.draw();

    for (GLuint unit = 0; unit < 3; ++unit) glBindSampler(unit, 0);
  }

  void ui() override { paramsUi(m_params); }

 private:
  Params m_params;
  gl::Program m_program;
  gl::Mesh m_mesh;
  gl::Texture m_diffuse, m_normal, m_specular;
  gl::Sampler m_sampler;
  gl::Buffer m_uniforms;
};

GLINT_REGISTER_GL("06_normal_mapping", NormalMappingGL);

}  // namespace glint::normal_mapping
