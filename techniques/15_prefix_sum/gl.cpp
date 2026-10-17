// 15_prefix_sum — OpenGL. Stream compaction with a multi-level scan, all compute:
//   flags -> scan level 0, 1, ... (512-element blocks) -> add block offsets back down -> scatter -> draw
// Every compute pass is followed by glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT): the next pass reads through
// an SSBO what this one wrote through an SSBO. GL_TIMESTAMP queries between the phases time them on the GPU.
// Shaders are shared with VK.

#include <algorithm>
#include <array>

#include "core/log.h"
#include "gl/buffer.h"
#include "gl/gl.h"
#include "gl/shader.h"
#include "gl/technique.h"
#include "params.h"

namespace glint::prefix_sum {

namespace {
constexpr GLuint kPushBinding = 15;
// GL_KHR_shader_subgroup tokens (not in our glad, which only has GL 4.6 core + KHR_debug).
constexpr GLenum kSubgroupSize = 0x9532;               // GL_SUBGROUP_SIZE_KHR
constexpr GLenum kSubgroupSupportedStages = 0x9533;    // GL_SUBGROUP_SUPPORTED_STAGES_KHR
constexpr GLenum kSubgroupSupportedFeatures = 0x9534;  // GL_SUBGROUP_SUPPORTED_FEATURES_KHR
constexpr GLint kSubgroupFeatureArithmetic = 0x4;      // GL_SUBGROUP_FEATURE_ARITHMETIC_BIT_KHR

bool hasExtension(const char* name) {
  GLint count = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &count);
  for (GLint i = 0; i < count; ++i) {
    if (std::string_view(reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, GLuint(i)))) == name) return true;
  }
  return false;
}

GLuint groups(uint32_t n) { return (n + kThreads - 1) / kThreads; }
}  // namespace

class PrefixSumGL final : public gl::Technique {
 public:
  void init() override {
    const std::string dir = "15_prefix_sum/shaders/";
    m_flagsProgram = gl::Program({dir + "flags.comp"});
    m_scanShared = gl::Program({dir + "scan_shared.comp"});
    m_addProgram = gl::Program({dir + "add_offsets.comp"});
    m_scatterProgram = gl::Program({dir + "scatter.comp"});
    m_pointsProgram = gl::Program({dir + "points.vert", dir + "points.frag"});

    // Subgroups are an *extension* in GL (GL_KHR_shader_subgroup), with the same three questions VK asks through
    // VkPhysicalDeviceSubgroupProperties: how big, in which stages, which operations.
    GLint stages = 0, features = 0;
    if (hasExtension("GL_KHR_shader_subgroup")) {
      glGetIntegerv(kSubgroupSize, &m_subgroupSize);
      glGetIntegerv(kSubgroupSupportedStages, &stages);
      glGetIntegerv(kSubgroupSupportedFeatures, &features);
    }
    log::info("15_prefix_sum: subgroup size {}, stages 0x{:x}, features 0x{:x}", m_subgroupSize, stages, features);
    if ((stages & GL_COMPUTE_SHADER_BIT) && (features & kSubgroupFeatureArithmetic)) {
      m_scanSubgroup = gl::Program({dir + "scan_subgroup.comp"});
    } else {
      log::warn("15_prefix_sum: no subgroup arithmetic in GL compute shaders; using the shared-memory kernel only");
      m_params.kernel = 0;
    }

    // Three storage buffers, sized for the maximum count. Flags 0: GPU-only (the CPU check copies them out).
    m_scan = gl::Buffer(scanBufferElements(kMaxPoints) * sizeof(uint32_t), nullptr, 0, "15 scan levels");
    m_flags = gl::Buffer(kMaxPoints * sizeof(uint32_t), nullptr, 0, "15 flags");
    m_compact = gl::Buffer((kMaxPoints + 1) * sizeof(uint32_t), nullptr, 0, "15 compact");
    m_pushUbo = gl::Buffer(sizeof(PassConstants), nullptr, GL_DYNAMIC_STORAGE_BIT, "15 push constants");
    glCreateVertexArrays(1, &m_emptyVao);

    for (TimestampSet& set : m_timestamps) glCreateQueries(GL_TIMESTAMP, GLsizei(set.ids.size()), set.ids.data());
  }

  ~PrefixSumGL() override {
    log::info("15_prefix_sum timing: {}", m_timingLog.summary(m_params));
    glDeleteVertexArrays(1, &m_emptyVao);
    glDeleteBuffers(1, &m_readback);
    for (TimestampSet& set : m_timestamps) glDeleteQueries(GLsizei(set.ids.size()), set.ids.data());
  }

  void update(float dt, Frame& /*frame*/) override {
    for (gl::Program* p : {&m_flagsProgram, &m_scanShared, &m_scanSubgroup, &m_addProgram, &m_scatterProgram,
                           &m_pointsProgram}) {
      if (p->id()) p->reloadIfChanged();
    }
    if (!m_params.paused) m_time += dt;
    m_autoVerify.step(m_params, m_scanSubgroup.id() != 0);
    if (!m_scanSubgroup.id()) m_params.kernel = 0;
  }

  void render(const Frame& frame) override {
    collectTimestamps();
    TimestampSet& ts = m_timestamps[m_tsCurrent];
    m_tsCurrent = (m_tsCurrent + 1) % m_timestamps.size();

    const uint32_t n = m_params.count();
    const uint32_t width = m_params.gridWidth();
    const std::vector<ScanLevel> levels = scanLevels(n);
    const glm::vec4 params(m_time, frame.aspect(), 0.0f, 0.0f);
    glBindBufferBase(GL_UNIFORM_BUFFER, kPushBinding, m_pushUbo.id());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_scan.id());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_flags.id());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_compact.id());

    // A timestamp is taken when every earlier command has *finished* on the GPU. VK: vkCmdWriteTimestamp2.
    glQueryCounter(ts.ids[0], GL_TIMESTAMP);

    // --- flags ----------------------------------------------------------------------------------------------
    m_flagsProgram.use();
    m_pushUbo.update(PassConstants{{n, 0, 0, width}, params});
    glDispatchCompute(groups(n), 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);  // next use: SSBO reads in the next compute pass
    glQueryCounter(ts.ids[1], GL_TIMESTAMP);

    // --- scan: up the levels, then add each level's block offsets back down ----------------------------------
    (m_params.kernel == 1 ? m_scanSubgroup : m_scanShared).use();
    for (size_t k = 0; k < levels.size(); ++k) {
      m_pushUbo.update(PassConstants{{levels[k].count, levels[k].offset, sumsOffset(levels, k), width}, params});
      glDispatchCompute(levels[k].blocks, 1, 1);  // one workgroup per 512-element block
      glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
    m_addProgram.use();
    for (size_t k = levels.size() - 1; k-- > 0;) {
      m_pushUbo.update(PassConstants{{levels[k].count, levels[k].offset, levels[k + 1].offset, width}, params});
      glDispatchCompute(groups(levels[k].count), 1, 1);
      glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
    glQueryCounter(ts.ids[2], GL_TIMESTAMP);

    // --- scatter --------------------------------------------------------------------------------------------
    m_scatterProgram.use();
    m_pushUbo.update(PassConstants{{n, 0, 0, width}, params});
    glDispatchCompute(groups(n), 1, 1);
    // Next uses: the vertex shader reads `compact` as an SSBO (SHADER_STORAGE again — unlike 14, which read its
    // buffer as vertex attributes), and the CPU check copies with glCopyNamedBufferSubData (BUFFER_UPDATE).
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | (m_params.verify ? GL_BUFFER_UPDATE_BARRIER_BIT : 0));
    glQueryCounter(ts.ids[3], GL_TIMESTAMP);
    ts.pending = true;

    // --- draw -----------------------------------------------------------------------------------------------
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, frame.framebufferSize.x, frame.framebufferSize.y);
    glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_PROGRAM_POINT_SIZE);
    m_pointsProgram.use();
    glBindVertexArray(m_emptyVao);
    for (float mode : {0.0f, 1.0f}) {
      if (mode == 0.0f && !m_params.showRejected) continue;
      m_pushUbo.update(PassConstants{{n, 0, 0, width}, glm::vec4(m_time, frame.aspect(), mode, 0.0f)});
      glDrawArrays(GL_POINTS, 0, GLsizei(n));  // mode 1: vertices past the (GPU-side) count clip themselves
    }
    glDisable(GL_PROGRAM_POINT_SIZE);

    if (m_params.verify) verifyOnCpu(n);
  }

  void ui() override { paramsUi(m_params, m_timings, m_verifyResult, uint32_t(m_subgroupSize)); }

 private:
  struct TimestampSet {
    std::array<GLuint, 4> ids{};
    bool pending = false;
  };

  // Read the oldest set if the GPU has finished it — never wait. 4 sets: results take a few frames to arrive.
  void collectTimestamps() {
    TimestampSet& set = m_timestamps[m_tsCurrent];
    if (!set.pending) return;
    GLint available = 0;
    glGetQueryObjectiv(set.ids[3], GL_QUERY_RESULT_AVAILABLE, &available);
    if (!available) return;  // the set gets overwritten this frame; this sample is simply skipped
    std::array<GLuint64, 4> t{};
    for (size_t i = 0; i < 4; ++i) glGetQueryObjectui64v(set.ids[i], GL_QUERY_RESULT, &t[i]);
    m_timings = {double(t[1] - t[0]) * 1e-6, double(t[2] - t[1]) * 1e-6, double(t[3] - t[2]) * 1e-6};
    m_timingLog.add(m_timings, m_autoVerify.frame);
    set.pending = false;
  }

  // Reading the GPU buffers directly with glGetNamedBufferSubData works, but NVIDIA's driver then *migrates*
  // them from video to host memory (a performance warning per buffer) — it guesses from usage where a buffer
  // should live. So: the same staging pattern as VK. Copy on the GPU into a buffer that asked for CPU reads
  // (GL_MAP_READ_BIT | GL_CLIENT_STORAGE_BIT ~ VK HOST_VISIBLE | HOST_CACHED), then map it. Mapping waits for
  // the copies by itself (a full stall — fine for a button); VK waits on the fence explicitly.
  void verifyOnCpu(uint32_t n) {
    const GLsizeiptr words = n * sizeof(uint32_t), size = 3 * words + sizeof(uint32_t);
    if (m_readbackSize < size) {
      glDeleteBuffers(1, &m_readback);
      glCreateBuffers(1, &m_readback);
      glNamedBufferStorage(m_readback, size, nullptr, GL_MAP_READ_BIT | GL_CLIENT_STORAGE_BIT);
      glObjectLabel(GL_BUFFER, m_readback, -1, "15 readback");
      m_readbackSize = size;
    }
    glCopyNamedBufferSubData(m_flags.id(), m_readback, 0, 0, words);
    glCopyNamedBufferSubData(m_scan.id(), m_readback, 0, words, words);
    glCopyNamedBufferSubData(m_compact.id(), m_readback, 0, 2 * words, words + GLsizeiptr(sizeof(uint32_t)));
    const auto* data = static_cast<const uint32_t*>(glMapNamedBufferRange(m_readback, 0, size, GL_MAP_READ_BIT));
    const std::vector<uint32_t> flags(data, data + n), scanned(data + n, data + 2 * n),
        compact(data + 2 * n, data + 3 * n + 1);
    glUnmapNamedBuffer(m_readback);
    m_verifyResult = verify(flags, scanned, compact, n);
    log::info("15_prefix_sum verify ({}): {}", kKernels[m_params.kernel], m_verifyResult);
    m_params.verify = false;
  }

  Params m_params;
  float m_time = 0.0f;
  GLint m_subgroupSize = 0;
  gl::Program m_flagsProgram, m_scanShared, m_scanSubgroup, m_addProgram, m_scatterProgram, m_pointsProgram;
  gl::Buffer m_scan, m_flags, m_compact, m_pushUbo;
  GLuint m_emptyVao = 0;
  GLuint m_readback = 0;
  GLsizeiptr m_readbackSize = 0;
  std::array<TimestampSet, 4> m_timestamps;
  size_t m_tsCurrent = 0;
  Timings m_timings;
  std::string m_verifyResult;
  AutoVerify m_autoVerify;
  TimingLog m_timingLog;
};

GLINT_REGISTER_GL("15_prefix_sum", PrefixSumGL);

}  // namespace glint::prefix_sum
