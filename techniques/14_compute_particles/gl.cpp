// 14_compute_particles — OpenGL. Per frame:
//   1. compute: glDispatchCompute over the particle SSBO (integrate in place)
//   2. glMemoryBarrier: make those writes visible to the next *use* of the buffer (vertex attributes)
//   3. draw: the same buffer as a vertex buffer, GL_POINTS, additive into an RGBA16F target
//   4. tone map into the default framebuffer (12's shader)
// Shaders are shared with VK.

#include <algorithm>

#include "../12_hdr_tonemapping/params.h"
#include "core/camera.h"
#include "core/log.h"
#include "gl/buffer.h"
#include "gl/framebuffer.h"
#include "gl/gl.h"
#include "gl/shader.h"
#include "gl/technique.h"
#include "gl/texture.h"
#include "params.h"

namespace glint::particles {

namespace {
constexpr GLuint kPushBinding = 15;  // glint.glsl's PUSH_CONSTANTS block in GL
}  // namespace

class ParticlesGL final : public gl::Technique {
 public:
  void setupCamera(Camera& camera) override { camera.setHome(kCameraHome, kCameraTarget); }

  void init() override {
    // A compute program is a program with a single GL_COMPUTE_SHADER stage. VK: a separate *compute pipeline*
    // object (vkCreateComputePipelines), with its own pipeline layout and bind point.
    m_simProgram = gl::Program({"14_compute_particles/shaders/simulate.comp"});
    m_drawProgram =
        gl::Program({"14_compute_particles/shaders/particle.vert", "14_compute_particles/shaders/particle.frag"});
    m_tonemapProgram =
        gl::Program({"common/shaders/fullscreen.vert", "12_hdr_tonemapping/shaders/tonemap.frag"});

    // The particle buffer: GPU-only (flags 0 = no CPU access at all; GL_DYNAMIC_STORAGE_BIT only concerns
    // glNamedBufferSubData). It's a buffer like any other: what makes it an SSBO is binding it to
    // GL_SHADER_STORAGE_BUFFER, and the same name is bound as a vertex buffer below.
    // VK: usage STORAGE_BUFFER | VERTEX_BUFFER must be declared at creation.
    m_particles = gl::Buffer(kMaxParticles * sizeof(Particle), nullptr, 0, "14 particles");

    // Vertex layout of the draw = the struct in the SSBO: two vec4s, stride 32.
    glCreateVertexArrays(1, &m_vao);
    glVertexArrayVertexBuffer(m_vao, 0, m_particles.id(), 0, sizeof(Particle));
    for (GLuint a : {0u, 1u}) {
      glEnableVertexArrayAttrib(m_vao, a);
      glVertexArrayAttribFormat(m_vao, a, 4, GL_FLOAT, GL_FALSE, a * sizeof(glm::vec4));
      glVertexArrayAttribBinding(m_vao, a, 0);
    }
    glObjectLabel(GL_VERTEX_ARRAY, m_vao, -1, "14 particles as vertices");
    glCreateVertexArrays(1, &m_emptyVao);

    m_hdrSampler = gl::Sampler(GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, "linear clamp");
    m_pushUbo = gl::Buffer(std::max({sizeof(SimConstants), sizeof(DrawConstants), sizeof(hdr::TonemapConstants)}),
                           nullptr, GL_DYNAMIC_STORAGE_BIT, "14 push constants");
  }

  ~ParticlesGL() override {
    glDeleteVertexArrays(1, &m_vao);
    glDeleteVertexArrays(1, &m_emptyVao);
  }

  void update(float dt, Frame& frame) override {
    for (gl::Program* p : {&m_simProgram, &m_drawProgram, &m_tonemapProgram}) p->reloadIfChanged();
    if (frame.framebufferSize != m_size) createTargets(frame.framebufferSize);
    m_dt = simDt(dt, m_params);
    m_simTime += m_dt;
  }

  void render(const Frame& frame) override {
    glBindBufferBase(GL_UNIFORM_BUFFER, kPushBinding, m_pushUbo.id());

    // --- 1. simulate ----------------------------------------------------------------------------------------
    if (shouldSimulate(m_params)) {
      m_simProgram.use();
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_particles.id());  // SSBO binding 0
      m_pushUbo.update(makeSimConstants(m_params, m_dt, m_simTime));
      glDispatchCompute(groupCount(m_params), 1, 1);  // VK: vkCmdDispatch, same arguments

      // --- 2. the barrier ----------------------------------------------------------------------------------
      // The one place GL does *not* synchronize for you: writes through SSBOs (and images, atomics) are
      // "incoherent", and later commands may not see them without glMemoryBarrier. The bit names how the data
      // will be *read next* — here as vertex attributes — not how it was written. (GL_SHADER_STORAGE_BARRIER_BIT
      // would be the classic wrong guess: that covers later SSBO accesses, not attribute fetch.)
      // VK: a buffer barrier COMPUTE_SHADER/SHADER_STORAGE_WRITE -> VERTEX_ATTRIBUTE_INPUT/VERTEX_ATTRIBUTE_READ.
      glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
      m_params.reset = false;
    }

    // --- 3. draw the particles into the HDR target ----------------------------------------------------------
    m_hdrFbo.bind();
    glViewport(0, 0, m_size.x, m_size.y);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glEnable(GL_PROGRAM_POINT_SIZE);  // use the shader's gl_PointSize (VK: always)
    m_drawProgram.use();
    m_pushUbo.update(DrawConstants{frame.camera.viewProjection(frame.aspect(), ClipDepth::NegOneToOne),
                                   glm::vec4(m_params.brightness, 0.0f, 0.0f, 0.0f)});
    glBindVertexArray(m_vao);
    glDrawArrays(GL_POINTS, 0, GLsizei(m_params.count()));  // VK: topology POINT_LIST is pipeline state
    glDisable(GL_PROGRAM_POINT_SIZE);
    glDisable(GL_BLEND);

    // --- 4. tone map ----------------------------------------------------------------------------------------
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, frame.framebufferSize.x, frame.framebufferSize.y);
    m_tonemapProgram.use();
    m_hdr.bind(0);
    m_hdrSampler.bind(0);
    m_pushUbo.update(hdr::TonemapConstants{glm::vec4(std::exp2(m_params.exposureEv), 2.0f, 1.0f, 0.0f)});
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindSampler(0, 0);
  }

  void ui() override { paramsUi(m_params); }

 private:
  void createTargets(glm::ivec2 size) {
    m_size = size;
    m_hdr = gl::Texture(GL_RGBA16F, size.x, size.y, 1, "14 hdr color");
    m_hdrFbo = gl::Framebuffer("14 hdr fbo");
    m_hdrFbo.attachColor(m_hdr);
    m_hdrFbo.checkComplete();
  }

  Params m_params;
  float m_dt = 0.0f, m_simTime = 0.0f;
  gl::Program m_simProgram, m_drawProgram, m_tonemapProgram;
  gl::Buffer m_particles, m_pushUbo;
  GLuint m_vao = 0, m_emptyVao = 0;
  gl::Texture m_hdr;
  gl::Sampler m_hdrSampler;
  gl::Framebuffer m_hdrFbo;
  glm::ivec2 m_size{0};
};

GLINT_REGISTER_GL("14_compute_particles", ParticlesGL);

}  // namespace glint::particles
