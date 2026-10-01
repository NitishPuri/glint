// 11_gltf — OpenGL. One gl::Mesh per glTF primitive, one gl::Texture per used image; per draw: set the
// draw's uniforms, rebind textures when the material changes, toggle culling for double-sided materials.

#include <vector>

#include "core/assets.h"
#include "core/camera.h"
#include "core/gltf.h"
#include "core/log.h"
#include "gl/buffer.h"
#include "gl/gl.h"
#include "gl/mesh.h"
#include "gl/shader.h"
#include "gl/technique.h"
#include "gl/texture.h"
#include "params.h"

namespace glint::gltf_scene {

class GltfGL final : public gl::Technique {
 public:
  void init() override {
    m_model = loadGltf(assetPath(kModelPath));
    fillMissingAttributes(m_model);
    m_program = gl::Program({"11_gltf/shaders/model.gl.vert", "11_gltf/shaders/model.gl.frag"});
    for (size_t i = 0; i < m_model.primitives.size(); ++i) {
      m_meshes.emplace_back(m_model.primitives[i].mesh, fmt::format("gltf primitive {}", i));
    }
    // Images the materials don't use were never decoded (width 0): leave an empty texture in their slot.
    m_textures.resize(m_model.images.size());
    for (size_t i = 0; i < m_model.images.size(); ++i) {
      if (m_model.images[i].width > 0) {
        m_textures[i] = gl::Texture(m_model.images[i], true, fmt::format("gltf image {}", i));
      }
    }
    // Stand-ins for missing textures: white base color, flat (0, 0, 1) normal.
    m_white = gl::Texture(solidImage(255, 255, 255), false, "white");
    m_flatNormal = gl::Texture(solidImage(128, 128, 255), false, "flat normal");
    m_sampler = gl::Sampler(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT, "trilinear repeat");
    m_frameUbo = gl::Buffer(sizeof(FrameUniforms), nullptr, GL_DYNAMIC_STORAGE_BIT, "11 frame");
  }

  void setupCamera(Camera& camera) override { frameModel(camera, m_model); }

  void update(float /*dt*/, Frame& frame) override {
    m_program.reloadIfChanged();
    m_frameUbo.update(makeFrameUniforms(frame.camera, frame.aspect(), ClipDepth::NegOneToOne, m_params));
  }

  void render(const Frame& frame) override {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, frame.framebufferSize.x, frame.framebufferSize.y);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glCullFace(GL_BACK);

    m_program.use();
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_frameUbo.id());  // per frame: once
    m_sampler.bind(0);
    m_sampler.bind(1);

    int boundMaterial = -2;  // nothing bound yet
    for (const GltfDraw& draw : m_model.draws) {
      const int material = m_model.primitives[size_t(draw.primitive)].material;
      if (material != boundMaterial) {  // per material: rebind the two texture units
        const GltfMaterial mat = material >= 0 ? m_model.materials[size_t(material)] : GltfMaterial{};
        textureOr(mat.baseColorImage, m_white).bind(0);
        textureOr(mat.normalImage, m_flatNormal).bind(1);
        if (mat.doubleSided) {
          glDisable(GL_CULL_FACE);
        } else {
          glEnable(GL_CULL_FACE);
        }
        boundMaterial = material;
      }
      // Per draw: three uniforms. VK: one vkCmdPushConstants of the same 96 bytes.
      const DrawConstants dc = makeDrawConstants(m_model, draw);
      m_program.set(0, dc.model);
      m_program.set(1, dc.baseColorFactor);
      m_program.set(2, dc.material);
      m_meshes[size_t(draw.primitive)].draw();
    }
    glBindSampler(0, 0);
    glBindSampler(1, 0);
  }

  void ui() override { paramsUi(m_params, m_model); }

 private:
  static ImageData solidImage(uint8_t r, uint8_t g, uint8_t b) { return {1, 1, 4, {r, g, b, 255}}; }
  const gl::Texture& textureOr(int image, const gl::Texture& fallback) const {
    return image >= 0 && m_textures[size_t(image)] ? m_textures[size_t(image)] : fallback;
  }

  Params m_params;
  ModelData m_model;
  gl::Program m_program;
  std::vector<gl::Mesh> m_meshes;
  std::vector<gl::Texture> m_textures;
  gl::Texture m_white, m_flatNormal;
  gl::Sampler m_sampler;
  gl::Buffer m_frameUbo;
};

GLINT_REGISTER_GL("11_gltf", GltfGL);

}  // namespace glint::gltf_scene
