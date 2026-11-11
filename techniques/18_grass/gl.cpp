// 18_grass — OpenGL. One instanced draw for the whole field: glDrawArraysInstanced runs the blade strip once per
// record of the instance buffer. What makes a buffer "per instance" is one VAO call, glVertexArrayBindingDivisor:
// binding 1 advances once per instance instead of once per vertex. The wind texture is made by 16's generate.comp
// (its GLSL source, loaded from 16's folder), once, at init. Shaders are shared with VK.

#include <algorithm>

#include "core/camera.h"
#include "core/log.h"
#include "gl/buffer.h"
#include "gl/gl.h"
#include "gl/shader.h"
#include "gl/technique.h"
#include "gl/texture.h"
#include "gl/vertex_array.h"
#include "params.h"

namespace glint::grass {

namespace {
constexpr GLuint kPushBinding = 15;
}  // namespace

class GrassGL final : public gl::Technique {
 public:
  void setupCamera(Camera& camera) override { camera.setHome(kCameraHome); }

  void init() override {
    m_grassProgram = gl::Program({"18_grass/shaders/grass.vert", "18_grass/shaders/grass.frag"});
    m_groundProgram = gl::Program({"18_grass/shaders/ground.vert", "18_grass/shaders/ground.frag"});
    m_pushUbo = gl::Buffer(std::max(sizeof(DrawConstants), sizeof(noise::GenerateConstants)), nullptr,
                           GL_DYNAMIC_STORAGE_BIT, "18 push constants");
    m_sampler = gl::Sampler(GL_LINEAR, GL_LINEAR, GL_REPEAT, "wind: trilinear repeat (3D)");
    glCreateVertexArrays(1, &m_emptyVao);
    generateWind();
  }

  ~GrassGL() override {
    glDeleteTextures(1, &m_wind);
    glDeleteVertexArrays(1, &m_emptyVao);
  }

  void update(float dt, Frame& /*frame*/) override {
    m_grassProgram.reloadIfChanged();
    m_groundProgram.reloadIfChanged();
    m_time += dt;
    if (!(m_built == m_params.field)) build(m_params.field);
  }

  void render(const Frame& frame) override {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, frame.framebufferSize.x, frame.framebufferSize.y);
    glClearColor(0.62f, 0.74f, 0.86f, 1.0f);  // sky; grass.glsl's kSky is this, decoded to linear
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);  // blades are seen from both sides (VK: vkCmdSetCullMode NONE)

    glBindBufferBase(GL_UNIFORM_BUFFER, kPushBinding, m_pushUbo.id());
    m_pushUbo.update(makeDrawConstants(m_params, frame.camera.viewProjection(frame.aspect(), ClipDepth::NegOneToOne),
                                       frame.camera.eye, m_time, m_bladeCount));
    glBindTextureUnit(0, m_wind);
    m_sampler.bind(0);

    m_groundProgram.use();
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // The whole field: `count` vertices of the strip, `instancecount` times. GL_TRIANGLE_STRIP is an argument of
    // the draw here; in VK the topology is baked into the pipeline.
    m_grassProgram.use();
    m_vao.bind();
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, m_stripVertices, m_bladeCount);

    glBindSampler(0, 0);
  }

  void ui() override { paramsUi(m_params); }

 private:
  // 16's compute shader into a 3D texture, as 16 does it (image unit 0, layered), then a barrier for sampling.
  void generateWind() {
    const noise::Generation gen = windGeneration();
    const int n = gen.extent();
    glCreateTextures(GL_TEXTURE_3D, 1, &m_wind);
    glTextureStorage3D(m_wind, 1, GL_RGBA16F, n, n, n);
    glObjectLabel(GL_TEXTURE, m_wind, -1, "18 wind (16's noise)");
    gl::Program generate({"16_noise/shaders/generate.comp"});
    generate.use();
    glBindImageTexture(0, m_wind, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glBindBufferBase(GL_UNIFORM_BUFFER, kPushBinding, m_pushUbo.id());
    m_pushUbo.update(noise::makeGenerateConstants(gen));
    const glm::uvec3 groups = noise::groupCounts(gen);
    glDispatchCompute(groups.x, groups.y, groups.z);
    glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    // Sampled next, by the *vertex* shader this time: TEXTURE_FETCH covers every stage.
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    log::info("18_grass: wind texture {}^3 from 16_noise/generate.comp", n);
  }

  void build(const Field& field) {
    const std::vector<glm::vec2> strip = bladeStrip(field.segments);
    const std::vector<Blade> blades = generateBlades(field);
    // Fresh immutable buffers (glNamedBufferStorage can't resize). The old ones may still be in use by queued
    // draws: GL keeps them alive until those finish. VK: we wait for the GPU ourselves.
    m_strip = gl::Buffer(strip, 0, "18 blade strip");
    m_blades = gl::Buffer(blades, 0, "18 blades (instances)");
    m_vao = gl::VertexArray("18 grass");
    m_vao.vertexBuffer(0, m_strip, sizeof(glm::vec2));
    m_vao.attribute(0, 0, 2);  // aStrip
    m_vao.vertexBuffer(1, m_blades, sizeof(Blade));
    m_vao.attribute(1, 1, 4, GL_FLOAT, offsetof(Blade, rootAngle));  // aRoot
    m_vao.attribute(2, 1, 4, GL_FLOAT, offsetof(Blade, shape));      // aShape
    // The instancing call: binding 1 advances once per 1 instance (divisor 0 = per vertex, the default). It belongs
    // to the binding, so it applies to both attributes read from it. (Pre-4.3 GL: glVertexAttribDivisor, per
    // attribute.) VK: inputRate = VK_VERTEX_INPUT_RATE_INSTANCE in the pipeline's binding description.
    glVertexArrayBindingDivisor(m_vao.id(), 1, 1);

    m_stripVertices = GLsizei(strip.size());
    m_bladeCount = GLsizei(blades.size());
    m_built = field;
    log::info("18_grass: {} blades x {} vertices, instance buffer {:.1f} MB", m_bladeCount, m_stripVertices,
              double(blades.size() * sizeof(Blade)) / (1 << 20));
  }

  Params m_params;
  Field m_built{.size = 0};  // what the buffers hold (size 0: nothing yet)
  float m_time = 0.0f;
  gl::Program m_grassProgram, m_groundProgram;
  gl::Buffer m_pushUbo, m_strip, m_blades;
  gl::VertexArray m_vao;
  gl::Sampler m_sampler;
  GLuint m_wind = 0, m_emptyVao = 0;
  GLsizei m_stripVertices = 0, m_bladeCount = 0;
};

GLINT_REGISTER_GL("18_grass", GrassGL);

}  // namespace glint::grass
