// 17_reaction_diffusion — OpenGL. Two RGBA32F textures, A and B, alternate as source and destination: each step
// reads one and writes the other ("ping-pong"; GPU Gems 2 ch31's *feedback*). Two ways to run a step:
//   compute:  image units (imageLoad src, imageStore dst) + glMemoryBarrier after every step
//   fragment: an FBO per texture, draw a full-screen triangle into dst while sampling src — no barrier at all,
//             GL orders render-to-texture → texture reads by itself (exactly why 2005 GPGPU used it)
// Then a two-pass reduction sums v; the result lands in a persistently mapped buffer and is read when its fence
// has signalled — a readback that never stalls. Shaders are shared with VK.

#include <algorithm>
#include <array>
#include <vector>

#include "core/log.h"
#include "gl/buffer.h"
#include "gl/gl.h"
#include "gl/shader.h"
#include "gl/technique.h"
#include "gl/texture.h"
#include "params.h"

namespace glint::rd {

namespace {
constexpr GLuint kPushBinding = 15;
constexpr int kSlots = 4;  // results in flight; a slot is reused only after its fence was read (or skipped)
constexpr int kMaxGroups = (1024 / kGroup) * (1024 / kGroup);
}  // namespace

class ReactionDiffusionGL final : public gl::Technique {
 public:
  void init() override {
    const std::string dir = "17_reaction_diffusion/shaders/";
    m_seedProgram = gl::Program({dir + "seed.comp"});
    m_stepCompute = gl::Program({dir + "step.comp"});
    m_stepFragment = gl::Program({"common/shaders/screen.vert", dir + "step.frag"});
    m_reduceProgram = gl::Program({dir + "reduce.comp"});
    m_displayProgram = gl::Program({"common/shaders/screen.vert", dir + "display.frag"});
    // texelFetch ignores filtering, but an RGBA32F texture with the default (mipmapped) min filter and one level is
    // *incomplete* — sampling returns zeros. A sampler object with GL_NEAREST makes it complete.
    m_nearest = gl::Sampler(GL_NEAREST, GL_NEAREST, GL_REPEAT, "nearest repeat");
    m_linear = gl::Sampler(GL_LINEAR, GL_LINEAR, GL_REPEAT, "linear repeat");  // GL 4.x: float32 always filterable
    m_pushUbo = gl::Buffer(std::max({sizeof(StepConstants), sizeof(DisplayConstants), sizeof(ReduceConstants)}),
                           nullptr, GL_DYNAMIC_STORAGE_BIT, "17 push constants");
    m_partials = gl::Buffer(kMaxGroups * sizeof(float), nullptr, 0, "17 reduction partials");

    // The result ring: persistently mapped (it stays mapped while the GPU writes it) and coherent (no flush calls).
    // GL_MAP_PERSISTENT_BIT is what makes "GPU writes, CPU reads later without unmapping" legal.
    // VK: every HOST_VISIBLE buffer works like this; you just map it once.
    glCreateBuffers(1, &m_results);
    constexpr GLbitfield flags = GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
    glNamedBufferStorage(m_results, kSlots * sizeof(float), nullptr, flags);
    glObjectLabel(GL_BUFFER, m_results, -1, "17 reduction results (mapped)");
    m_mapped = static_cast<const float*>(glMapNamedBufferRange(m_results, 0, kSlots * sizeof(float), flags));
    glCreateVertexArrays(1, &m_emptyVao);
  }

  ~ReactionDiffusionGL() override {
    destroyTargets();
    for (GLsync& f : m_fences) glDeleteSync(f);
    glUnmapNamedBuffer(m_results);
    glDeleteBuffers(1, &m_results);
    glDeleteVertexArrays(1, &m_emptyVao);
  }

  void update(float /*dt*/, Frame& frame) override {
    for (gl::Program* p : {&m_seedProgram, &m_stepCompute, &m_stepFragment, &m_reduceProgram, &m_displayProgram}) {
      p->reloadIfChanged();
    }
    m_autoVerify.step(m_params);
    m_brushTexel = screenToTexel(frame.input.mousePos(), frame.framebufferSize, m_params.grid());
    m_brush = painting(frame.input) && !m_autoVerify.active();
    m_aspect = frame.aspect();
    ++m_frame;
  }

  void render(const Frame& frame) override {
    collectResults();
    if (m_params.grid() != m_grid) createTargets(m_params.grid());
    glBindBufferBase(GL_UNIFORM_BUFFER, kPushBinding, m_pushUbo.id());
    glBindVertexArray(m_emptyVao);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    const glm::uvec2 groups = groupCounts(m_grid);

    if (m_params.reset) {  // seed texture A with the compute pass
      m_seedProgram.use();
      glBindImageTexture(1, m_tex[0], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
      m_pushUbo.update(makeStepConstants(m_params, false, {}));
      glDispatchCompute(groups.x, groups.y, 1);
      // Next use: imageLoad (compute path) or texelFetch (fragment path) — name both.
      glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
      m_current = 0;
      m_steps = 0;
      m_painted = false;
      m_params.reset = false;
    }

    // --- the steps: src = m_current, dst = the other one, then swap ----------------------------------------------
    const int steps = m_params.paused ? 0 : m_params.stepsPerFrame;
    for (int s = 0; s < steps; ++s) {
      const int src = m_current, dst = 1 - m_current;
      m_pushUbo.update(makeStepConstants(m_params, m_brush && s == 0, m_brushTexel));
      if (m_params.path == 0) {
        m_stepCompute.use();
        glBindImageTexture(0, m_tex[src], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
        glBindImageTexture(1, m_tex[dst], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        glDispatchCompute(groups.x, groups.y, 1);
        // Name only the *next* use. Between steps that's imageLoad (SHADER_IMAGE_ACCESS). After the last step the
        // result is sampled (TEXTURE_FETCH: reduction, display, a fragment step) or read back (TEXTURE_UPDATE).
        // (Measured: on NVIDIA this choice doesn't change the time — see NOTES "GL compute is 3x slower".)
        const bool last = s == steps - 1;
        glMemoryBarrier(last ? GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
                                   GL_TEXTURE_UPDATE_BARRIER_BIT
                             : GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
      } else {
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo[dst]);
        glViewport(0, 0, m_grid.x, m_grid.y);
        m_stepFragment.use();
        glBindTextureUnit(0, m_tex[src]);
        m_nearest.bind(0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        // Nothing to synchronize: GL sees "rendered into m_tex[dst]" → "sampled next step" and orders it.
        // (Sampling m_tex[dst] *while* it's attached would be a feedback loop — undefined. Ping-pong avoids it.)
      }
      m_painted = m_painted || (m_brush && s == 0);
      m_current = dst;
      ++m_steps;
    }

    if (m_params.verify) verify();

    // --- reduction: total v → result slot -------------------------------------------------------------------
    const int slot = m_frame % kSlots;
    if (m_fences[size_t(slot)]) {  // never read back: skip that sample, reuse the slot
      glDeleteSync(m_fences[size_t(slot)]);
      m_fences[size_t(slot)] = nullptr;
    }
    m_reduceProgram.use();
    glBindTextureUnit(0, m_tex[m_current]);
    m_nearest.bind(0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_partials.id());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_results);
    m_pushUbo.update(ReduceConstants{glm::uvec4(uint32_t(m_grid.x), uint32_t(m_grid.y), 0u, uint32_t(slot))});
    glDispatchCompute(groups.x, groups.y, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    m_pushUbo.update(ReduceConstants{glm::uvec4(uint32_t(m_grid.x), uint32_t(m_grid.y), 1u, uint32_t(slot))});
    glDispatchCompute(1, 1, 1);
    // Shader writes to a persistently mapped buffer reach the CPU only after this barrier *and* once the GPU has
    // executed it — hence the fence. VK: barrier COMPUTE → HOST/HOST_READ, then the frame's fence.
    glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
    m_fences[size_t(slot)] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    m_issuedFrame[size_t(slot)] = m_frame;

    // --- display ----------------------------------------------------------------------------------------------
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, frame.framebufferSize.x, frame.framebufferSize.y);
    m_displayProgram.use();
    glBindTextureUnit(0, m_tex[m_current]);
    m_linear.bind(0);
    m_pushUbo.update(DisplayConstants{glm::vec4(m_aspect, float(m_params.palette), 0, 0),
                                      glm::vec4(m_brushTexel, m_params.brushRadius, 1.0f)});
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindSampler(0, 0);
  }

  void ui() override { paramsUi(m_params, m_history, m_latency, m_verifyResult); }

 private:
  // Read every slot whose fence has signalled — glClientWaitSync with a zero timeout only *asks*.
  void collectResults() {
    for (int i = 0; i < kSlots; ++i) {
      // oldest first, so the history stays in order
      const size_t s = size_t((m_frame + 1 + i) % kSlots);
      if (!m_fences[s]) continue;
      const GLenum status = glClientWaitSync(m_fences[s], 0, 0);
      if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED) continue;
      m_history.push(m_mapped[s]);
      m_latency = m_frame - m_issuedFrame[s];
      glDeleteSync(m_fences[s]);
      m_fences[s] = nullptr;
    }
  }

  void createTargets(glm::ivec2 grid) {
    destroyTargets();
    m_grid = grid;
    for (int i = 0; i < 2; ++i) {
      glCreateTextures(GL_TEXTURE_2D, 1, &m_tex[size_t(i)]);
      glTextureStorage2D(m_tex[size_t(i)], 1, GL_RGBA32F, grid.x, grid.y);
      const std::string label = fmt::format("17 state {}", i == 0 ? "A" : "B");
      glObjectLabel(GL_TEXTURE, m_tex[size_t(i)], -1, label.c_str());
      glCreateFramebuffers(1, &m_fbo[size_t(i)]);
      glNamedFramebufferTexture(m_fbo[size_t(i)], GL_COLOR_ATTACHMENT0, m_tex[size_t(i)], 0);
      glObjectLabel(GL_FRAMEBUFFER, m_fbo[size_t(i)], -1, label.c_str());
    }
    m_params.reset = true;
  }

  void destroyTargets() {
    glDeleteFramebuffers(2, m_fbo.data());
    glDeleteTextures(2, m_tex.data());
    m_fbo = {};
    m_tex = {};
  }

  void verify() {
    m_params.verify = false;
    if (m_painted) {  // the CPU reference knows nothing about brush strokes
      m_verifyResult = "skipped: painted since the last reset (reset, then verify before painting)";
      return;
    }
    const glm::ivec2 o = checkOrigin(m_grid);
    std::vector<glm::vec4> texels(kCheckBlock * kCheckBlock);
    glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);  // (fragment path: not needed; compute path: already done)
    glGetTextureSubImage(m_tex[m_current], 0, o.x, o.y, 0, kCheckBlock, kCheckBlock, 1, GL_RGBA, GL_FLOAT,
                         GLsizei(texels.size() * sizeof(glm::vec4)), texels.data());
    m_verifyResult = verifyBlock(texels.data(), m_params, m_steps, m_params.path);
    log::info("17_reaction_diffusion verify: {}", m_verifyResult);
    m_params.verify = false;
  }

  Params m_params;
  AutoVerify m_autoVerify;
  History m_history;
  int m_latency = 0, m_frame = 0, m_steps = 0, m_current = 0;
  bool m_brush = false, m_painted = false;
  glm::vec2 m_brushTexel{0.0f};
  float m_aspect = 1.0f;
  glm::ivec2 m_grid{0};
  gl::Program m_seedProgram, m_stepCompute, m_stepFragment, m_reduceProgram, m_displayProgram;
  gl::Sampler m_nearest, m_linear;
  gl::Buffer m_pushUbo, m_partials;
  std::array<GLuint, 2> m_tex{}, m_fbo{};
  GLuint m_results = 0, m_emptyVao = 0;
  const float* m_mapped = nullptr;
  std::array<GLsync, kSlots> m_fences{};
  std::array<int, kSlots> m_issuedFrame{};
  std::string m_verifyResult;
};

GLINT_REGISTER_GL("17_reaction_diffusion", ReactionDiffusionGL);

}  // namespace glint::rd
