// 16_noise — OpenGL. A compute shader writes four noises into a 3D RGBA16F texture through an *image unit*
// (glBindImageTexture + imageStore), then a full-screen pass samples that texture through an ordinary texture
// unit (sampler3D). Regenerated only when the generation parameters change (or every frame with "evolve").
// Shaders are shared with VK.

#include <algorithm>
#include <optional>
#include <vector>

#include "core/camera.h"
#include "core/log.h"
#include "gl/buffer.h"
#include "gl/gl.h"
#include "gl/shader.h"
#include "gl/technique.h"
#include "gl/texture.h"
#include "params.h"

namespace glint::noise {

namespace {
constexpr GLuint kPushBinding = 15;
}  // namespace

class NoiseGL final : public gl::Technique {
 public:
  void setupCamera(Camera& camera) override { camera.setHome(kCameraHome); }

  void init() override {
    m_generateProgram = gl::Program({"16_noise/shaders/generate.comp"});
    m_displayProgram = gl::Program({"common/shaders/screen.vert", "16_noise/shaders/display.frag"});
    // REPEAT on S, T *and R*: the noise tiles in all three directions. (gl::Sampler sets R since 16; before, a
    // CLAMP sampler would have clamped only two axes of a 3D texture. VK's sampler always set addressModeW.)
    m_sampler = gl::Sampler(GL_LINEAR, GL_LINEAR, GL_REPEAT, "trilinear repeat (3D)");
    m_pushUbo = gl::Buffer(std::max(sizeof(GenerateConstants), sizeof(DisplayConstants)), nullptr,
                           GL_DYNAMIC_STORAGE_BIT, "16 push constants");
    glCreateVertexArrays(1, &m_emptyVao);
  }

  ~NoiseGL() override {
    glDeleteTextures(1, &m_noise);
    glDeleteVertexArrays(1, &m_emptyVao);
  }

  void update(float dt, Frame& /*frame*/) override {
    // An edited noise.glsl reloads the programs; regenerate so the texture shows the new code.
    if (m_generateProgram.reloadIfChanged()) m_generated = {};
    m_displayProgram.reloadIfChanged();
    m_time += dt;
    if (m_params.evolve) m_params.gen.evolveOffset += dt * m_params.evolveSpeed;
    if (++m_frame == 10) m_params.verify = true;  // the startup check, logged (as in 15)
  }

  void render(const Frame& frame) override {
    glBindBufferBase(GL_UNIFORM_BUFFER, kPushBinding, m_pushUbo.id());
    const Generation& gen = m_params.gen;
    if (!m_generated || !(*m_generated == gen)) generate(gen);
    if (m_params.verify) verify(gen);

    // --- display: the 3D texture through a texture unit ---------------------------------------------------
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, frame.framebufferSize.x, frame.framebufferSize.y);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    m_displayProgram.use();
    glBindTextureUnit(0, m_noise);
    m_sampler.bind(0);
    m_pushUbo.update(makeDisplayConstants(m_params, frame.camera.viewProjection(frame.aspect(), ClipDepth::NegOneToOne),
                                          frame.camera.eye, frame.aspect(), m_time));
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindSampler(0, 0);
  }

  void ui() override { paramsUi(m_params, m_verifyResult); }

 private:
  void generate(const Generation& gen) {
    const int n = gen.extent();
    if (n != m_extent) {
      // A 3D texture: glTextureStorage3D, immutable, one level. VK: VkImageCreateInfo with imageType 3D.
      glDeleteTextures(1, &m_noise);
      glCreateTextures(GL_TEXTURE_3D, 1, &m_noise);
      glTextureStorage3D(m_noise, 1, GL_RGBA16F, n, n, n);
      glObjectLabel(GL_TEXTURE, m_noise, -1, "16 noise 3D");
      m_extent = n;
    }
    m_generateProgram.use();
    // Image unit 0 (not texture unit 0 — a separate set of binding points). layered = GL_TRUE binds all z slices
    // of the 3D texture; GL_FALSE would bind a single 2D slice. The format must match the shader's rgba16f.
    glBindImageTexture(0, m_noise, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    m_pushUbo.update(makeGenerateConstants(gen));
    const glm::uvec3 groups = groupCounts(gen);
    glDispatchCompute(groups.x, groups.y, groups.z);
    glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    // imageStore writes are incoherent, like SSBO writes: the next use is *sampling*, so TEXTURE_FETCH (and
    // TEXTURE_UPDATE for the CPU check's glGetTextureSubImage). VK: barrier GENERAL → SHADER_READ_ONLY.
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);
    m_generated = gen;
  }

  // glGetTextureSubImage converts RGBA16F to float for us (VK: copy out the halfs and convert on the CPU).
  void verify(const Generation& gen) {
    const glm::ivec3 o = checkOrigin(gen);
    std::vector<glm::vec4> texels(kCheckBlock * kCheckBlock * kCheckBlock);
    glGetTextureSubImage(m_noise, 0, o.x, o.y, o.z, kCheckBlock, kCheckBlock, kCheckBlock, GL_RGBA, GL_FLOAT,
                         GLsizei(texels.size() * sizeof(glm::vec4)), texels.data());
    m_verifyResult = verifyBlock(texels.data(), gen);
    log::info("16_noise verify: {}", m_verifyResult);
    m_params.verify = false;
  }

  Params m_params;
  std::optional<Generation> m_generated;  // what the texture holds
  float m_time = 0.0f;
  int m_frame = 0;
  gl::Program m_generateProgram, m_displayProgram;
  gl::Sampler m_sampler;
  gl::Buffer m_pushUbo;
  GLuint m_noise = 0, m_emptyVao = 0;
  int m_extent = 0;
  std::string m_verifyResult;
};

GLINT_REGISTER_GL("16_noise", NoiseGL);

}  // namespace glint::noise
