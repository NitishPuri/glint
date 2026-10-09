// 13_bloom — OpenGL. 12's HDR room, then a bloom chain and a graded composite:
//   pass 1        scene -> RGBA16F
//   pass 2..L+1   downsample: HDR -> bloom level 0 (prefilter) -> level 1 -> ... -> level L-1
//   pass ..       upsample:   level L-1 -> L-2 -> ... -> 0, each *added* (blend ONE, ONE) onto the level
//   last          composite:  HDR + bloom, tone map, sRGB encode, grading LUT -> default framebuffer
// The bloom levels are the mip levels of *one* texture. Each level gets a texture view (GL 4.3) that is used
// both as the attachment when rendering it and as the source when sampling it: the GL counterpart of a
// VkImageView per mip. Shaders are shared with VK.

#include <algorithm>
#include <array>

#include "core/assets.h"
#include "core/camera.h"
#include "core/log.h"
#include "gl/buffer.h"
#include "gl/framebuffer.h"
#include "gl/gl.h"
#include "gl/mesh.h"
#include "gl/shader.h"
#include "gl/technique.h"
#include "gl/texture.h"
#include "params.h"

namespace glint::bloom {

namespace {
constexpr GLuint kPushBinding = 15;  // glint.glsl's PUSH_CONSTANTS block in GL
}  // namespace

class BloomGL final : public gl::Technique {
 public:
  void setupCamera(Camera& camera) override { camera.setHome(hdr::kCameraHome); }

  void init() override {
    // The scene pass is 12's, unchanged.
    m_sceneProgram = gl::Program({"12_hdr_tonemapping/shaders/scene.vert", "12_hdr_tonemapping/shaders/scene.frag"});
    m_downsampleProgram = gl::Program({"common/shaders/fullscreen.vert", "13_bloom/shaders/downsample.frag"});
    m_upsampleProgram = gl::Program({"common/shaders/fullscreen.vert", "13_bloom/shaders/upsample.frag"});
    m_compositeProgram = gl::Program({"common/shaders/fullscreen.vert", "13_bloom/shaders/composite.frag"});
    MeshData room = loadObj(assetPath("room/room_thickwalls.obj"));
    indexMesh(room);
    m_room = gl::Mesh(room, "room");
    m_cube = gl::Mesh(makeCube(), "light marker");
    m_albedo = gl::Texture(loadImage(assetPath("room/uvmap.jpg")), true, "room uvmap (sRGB)", /*srgb*/ true);
    m_albedoSampler = gl::Sampler(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT, "trilinear repeat");
    // GL_LINEAR minification: each view holds a single level, so there is nothing to choose between anyway.
    m_linearClamp = gl::Sampler(GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, "linear clamp");

    // The grading LUT: plain RGBA8 (the table maps display-encoded values to display-encoded values).
    m_lut = gl::Texture(makeGradingLut(m_params.grading), false, "13 grading LUT");
    m_lutGrading = m_params.grading;

    m_sceneUbo = gl::Buffer(sizeof(hdr::SceneUniforms), nullptr, GL_DYNAMIC_STORAGE_BIT, "13 scene");
    m_pushUbo = gl::Buffer(std::max({sizeof(hdr::DrawConstants), sizeof(DownsampleConstants),
                                     sizeof(UpsampleConstants), sizeof(CompositeConstants)}),
                           nullptr, GL_DYNAMIC_STORAGE_BIT, "13 push constants");
    glCreateVertexArrays(1, &m_emptyVao);
  }

  ~BloomGL() override {
    destroyBloomLevels();
    glDeleteVertexArrays(1, &m_emptyVao);
  }

  void update(float /*dt*/, Frame& frame) override {
    for (gl::Program* p : {&m_sceneProgram, &m_downsampleProgram, &m_upsampleProgram, &m_compositeProgram}) {
      p->reloadIfChanged();
    }
    if (frame.framebufferSize != m_size) createTargets(frame.framebufferSize);
    m_params.scene.linearWorkflow = true;
    m_sceneUbo.update(hdr::makeSceneUniforms(frame.camera.viewProjection(frame.aspect(), ClipDepth::NegOneToOne),
                                             frame.camera.eye, m_params.scene));

    // Re-bake the LUT when a grading control moved. GL: one call; the driver makes sure frames still in
    // flight see the old contents (it copies, or waits). VK: we record the copy and the barriers ourselves.
    if (m_params.grading != m_lutGrading) {
      const ImageData lut = makeGradingLut(m_params.grading);
      glTextureSubImage2D(m_lut.id(), 0, 0, 0, lut.width, lut.height, GL_RGBA, GL_UNSIGNED_BYTE, lut.pixels.data());
      m_lutGrading = m_params.grading;
    }
  }

  void render(const Frame& frame) override {
    // --- pass 1: scene -> RGBA16F, exactly as 12 ------------------------------------------------------------
    m_hdrFbo.bind();
    glViewport(0, 0, m_size.x, m_size.y);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    m_sceneProgram.use();
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_sceneUbo.id());
    glBindBufferBase(GL_UNIFORM_BUFFER, kPushBinding, m_pushUbo.id());
    m_albedo.bind(1);
    m_albedoSampler.bind(1);
    m_pushUbo.update(hdr::DrawConstants{glm::mat4(1.0f), glm::vec4(0.0f)});
    m_room.draw();
    for (int i = 0; i < hdr::kLights; ++i) {
      m_pushUbo.update(hdr::lightMarker(m_params.scene, i));
      m_cube.draw();
    }

    // Every post pass: a full-screen triangle, no depth, no culling, one source texture on unit 0.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(m_emptyVao);
    m_linearClamp.bind(0);
    const int levels = m_params.levels;

    // --- downsample chain -----------------------------------------------------------------------------------
    // No barriers anywhere below: GL sees "rendered into view i, now sampled" and orders it for us. VK needs a
    // barrier (and a layout change) per level, per direction.
    m_downsampleProgram.use();
    for (int i = 0; i < levels; ++i) {
      glBindFramebuffer(GL_FRAMEBUFFER, m_levelFbos[size_t(i)]);
      const glm::ivec2 size = levelSize(m_size, i);
      glViewport(0, 0, size.x, size.y);
      glBindTextureUnit(0, i == 0 ? m_hdr.id() : m_levelViews[size_t(i - 1)]);
      m_pushUbo.update(makeDownsampleConstants(m_params, i == 0));
      glDrawArrays(GL_TRIANGLES, 0, 3);  // overwrites every pixel: no clear needed
    }

    // --- upsample chain: level i += tent(level i+1) ---------------------------------------------------------
    // Sampling level i+1 while rendering level i of the *same* texture is fine: the feedback-loop rule only
    // forbids sampling the levels that are attached (and each view exposes exactly one level).
    m_upsampleProgram.use();
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);  // VK: fixed in the pipeline (additiveBlend), plus LOAD_OP_LOAD on the target
    m_pushUbo.update(UpsampleConstants{glm::vec4(m_params.radius, 0.0f, 0.0f, 0.0f)});
    for (int i = levels - 2; i >= 0; --i) {
      glBindFramebuffer(GL_FRAMEBUFFER, m_levelFbos[size_t(i)]);
      const glm::ivec2 size = levelSize(m_size, i);
      glViewport(0, 0, size.x, size.y);
      glBindTextureUnit(0, m_levelViews[size_t(i + 1)]);
      glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glDisable(GL_BLEND);

    // --- composite -> default framebuffer -------------------------------------------------------------------
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, frame.framebufferSize.x, frame.framebufferSize.y);
    m_compositeProgram.use();
    m_hdr.bind(0);
    glBindTextureUnit(1, m_levelViews[0]);
    m_lut.bind(2);
    m_linearClamp.bind(1);
    m_linearClamp.bind(2);
    m_pushUbo.update(makeCompositeConstants(m_params));
    glDrawArrays(GL_TRIANGLES, 0, 3);

    for (GLuint unit : {0u, 1u, 2u}) glBindSampler(unit, 0);
  }

  void ui() override { paramsUi(m_params); }

 private:
  void createTargets(glm::ivec2 size) {
    m_size = size;
    m_hdr = gl::Texture(GL_RGBA16F, size.x, size.y, 1, "13 hdr color");
    m_depth = gl::Texture(GL_DEPTH_COMPONENT32F, size.x, size.y, 1, "13 depth");
    m_hdrFbo = gl::Framebuffer("13 hdr fbo");
    m_hdrFbo.attachColor(m_hdr);
    m_hdrFbo.attachDepth(m_depth);
    m_hdrFbo.checkComplete();

    // The bloom chain: one texture, kMaxLevels mip levels, starting at half resolution.
    // GL_R11F_G11F_B10F: three small unsigned floats in 32 bits — half the memory/bandwidth of RGBA16F, no
    // alpha, no negatives; the usual bloom format. VK: VK_FORMAT_B10G11R11_UFLOAT_PACK32 (same bits, the name
    // lists components from the most significant bit).
    destroyBloomLevels();
    const glm::ivec2 base = levelSize(size, 0);
    m_bloom = gl::Texture(GL_R11F_G11F_B10F, base.x, base.y, kMaxLevels, "13 bloom chain");
    for (int i = 0; i < kMaxLevels; ++i) {
      // A view: a new texture *name* aliasing level i of m_bloom's storage. Needs immutable storage
      // (glTextureStorage2D), and must be created with glGenTextures: glTextureView wants a name that has
      // never been bound, which glCreateTextures would already have initialized.
      GLuint& view = m_levelViews[size_t(i)];
      glGenTextures(1, &view);
      glTextureView(view, GL_TEXTURE_2D, m_bloom.id(), GL_R11F_G11F_B10F, GLuint(i), 1, 0, 1);
      const std::string label = fmt::format("13 bloom level {}", i);
      glObjectLabel(GL_TEXTURE, view, -1, label.c_str());

      GLuint& fbo = m_levelFbos[size_t(i)];
      glCreateFramebuffers(1, &fbo);
      glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, view, 0);  // level 0 *of the view* = level i
      glObjectLabel(GL_FRAMEBUFFER, fbo, -1, label.c_str());
      const GLenum status = glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER);
      if (status != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error(fmt::format("{} incomplete: 0x{:x}", label, status));
      }
    }
  }

  void destroyBloomLevels() {
    glDeleteFramebuffers(kMaxLevels, m_levelFbos.data());
    glDeleteTextures(kMaxLevels, m_levelViews.data());
    m_levelFbos.fill(0);
    m_levelViews.fill(0);
  }

  Params m_params;
  Grading m_lutGrading;  // what the LUT currently holds
  gl::Program m_sceneProgram, m_downsampleProgram, m_upsampleProgram, m_compositeProgram;
  gl::Mesh m_room, m_cube;
  gl::Texture m_albedo, m_hdr, m_depth, m_bloom, m_lut;
  gl::Sampler m_albedoSampler, m_linearClamp;
  gl::Framebuffer m_hdrFbo;
  std::array<GLuint, kMaxLevels> m_levelViews{}, m_levelFbos{};
  gl::Buffer m_sceneUbo, m_pushUbo;
  GLuint m_emptyVao = 0;
  glm::ivec2 m_size{0};
};

GLINT_REGISTER_GL("13_bloom", BloomGL);

}  // namespace glint::bloom
