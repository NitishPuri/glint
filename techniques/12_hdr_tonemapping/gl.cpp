// 12_hdr_tonemapping — OpenGL. Pass 1: the lit room into an RGBA16F texture (linear, unclamped). Pass 2: a
// full-screen triangle applies exposure + tone mapping + sRGB encoding into the default framebuffer.
// Shaders are shared with VK (one .vert/.frag each, see techniques/common/shaders/glint.glsl).

#include <algorithm>

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

namespace glint::hdr {

namespace {
// The shared shaders' PUSH_CONSTANTS block is a uniform block at this binding point in GL (glint.glsl).
constexpr GLuint kPushBinding = 15;
}  // namespace

class HdrGL final : public gl::Technique {
 public:
  void setupCamera(Camera& camera) override { camera.setHome(kCameraHome); }

  void init() override {
    m_sceneProgram = gl::Program({"12_hdr_tonemapping/shaders/scene.vert", "12_hdr_tonemapping/shaders/scene.frag"});
    m_tonemapProgram =
        gl::Program({"12_hdr_tonemapping/shaders/fullscreen.vert", "12_hdr_tonemapping/shaders/tonemap.frag"});
    MeshData room = loadObj(assetPath("room/room_thickwalls.obj"));
    indexMesh(room);
    m_room = gl::Mesh(room, "room");
    m_cube = gl::Mesh(makeCube(), "light marker");
    // sRGB storage: the texture's bytes are sRGB-encoded (like nearly every color image), so let the
    // hardware decode them to linear when sampling. 01-11 used GL_RGBA8 and lit the raw bytes.
    m_albedo = gl::Texture(loadImage(assetPath("room/uvmap.jpg")), true, "room uvmap (sRGB)", /*srgb*/ true);
    m_albedoSampler = gl::Sampler(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT, "trilinear repeat");
    m_hdrSampler = gl::Sampler(GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, "linear clamp");
    m_sceneUbo = gl::Buffer(sizeof(SceneUniforms), nullptr, GL_DYNAMIC_STORAGE_BIT, "12 scene");
    m_pushUbo = gl::Buffer(std::max(sizeof(DrawConstants), sizeof(TonemapConstants)), nullptr, GL_DYNAMIC_STORAGE_BIT,
                           "12 push constants");
    glCreateVertexArrays(1, &m_emptyVao);
  }

  ~HdrGL() override { glDeleteVertexArrays(1, &m_emptyVao); }

  void update(float /*dt*/, Frame& frame) override {
    m_sceneProgram.reloadIfChanged();
    m_tonemapProgram.reloadIfChanged();
    if (frame.framebufferSize != m_size) createTargets(frame.framebufferSize);
    m_sceneUbo.update(makeSceneUniforms(frame.camera.viewProjection(frame.aspect(), ClipDepth::NegOneToOne),
                                        frame.camera.eye, m_params));
  }

  void render(const Frame& frame) override {
    // --- pass 1: scene -> RGBA16F (linear radiance; values far above 1 survive) ----------------------------
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

    // GL "push constants": rewrite the small block before each draw (VK: vkCmdPushConstants).
    m_pushUbo.update(DrawConstants{glm::mat4(1.0f), glm::vec4(0.0f)});
    m_room.draw();
    for (int i = 0; i < kLights; ++i) {
      m_pushUbo.update(lightMarker(m_params, i));
      m_cube.draw();
    }

    // --- pass 2: tone map into the default framebuffer -------------------------------------------------------
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, frame.framebufferSize.x, frame.framebufferSize.y);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    m_tonemapProgram.use();
    m_hdr.bind(0);
    m_hdrSampler.bind(0);
    m_pushUbo.update(makeTonemapConstants(m_params));
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindSampler(0, 0);
    glBindSampler(1, 0);
  }

  void ui() override { paramsUi(m_params); }

 private:
  void createTargets(glm::ivec2 size) {
    m_size = size;
    // GL_RGBA16F: half floats, ~3 decimal digits, range up to 65504 — plenty for radiance. (RGBA8 would clip
    // every value above 1 *before* tone mapping could compress it.)
    m_hdr = gl::Texture(GL_RGBA16F, size.x, size.y, 1, "12 hdr color");
    m_depth = gl::Texture(GL_DEPTH_COMPONENT32F, size.x, size.y, 1, "12 depth");
    m_hdrFbo = gl::Framebuffer("12 hdr fbo");
    m_hdrFbo.attachColor(m_hdr);
    m_hdrFbo.attachDepth(m_depth);
    m_hdrFbo.checkComplete();
  }

  Params m_params;
  gl::Program m_sceneProgram, m_tonemapProgram;
  gl::Mesh m_room, m_cube;
  gl::Texture m_albedo, m_hdr, m_depth;
  gl::Sampler m_albedoSampler, m_hdrSampler;
  gl::Framebuffer m_hdrFbo;
  gl::Buffer m_sceneUbo, m_pushUbo;
  GLuint m_emptyVao = 0;
  glm::ivec2 m_size{0};
};

GLINT_REGISTER_GL("12_hdr_tonemapping", HdrGL);

}  // namespace glint::hdr
