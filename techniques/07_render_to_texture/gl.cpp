// 07_render_to_texture — OpenGL. Framebuffer objects written raw (gl::Framebuffer comes after this).
// Pass 1 renders the lit mesh into offscreen color + depth textures; pass 2 draws a full-screen triangle
// that reads those textures with a post-process distortion.

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

namespace glint::render_to_texture {

class RenderToTextureGL final : public gl::Technique {
 public:
  void init() override {
    m_sceneProgram = gl::Program({"common/shaders/shading.vert", "common/shaders/shading.frag"});
    m_postProgram =
        gl::Program({"07_render_to_texture/shaders/fullscreen.gl.vert", "07_render_to_texture/shaders/post.gl.frag"});
    MeshData suzanne = loadObj(assetPath("suzanne.obj"));
    indexMesh(suzanne);
    m_mesh = gl::Mesh(suzanne, "suzanne");
    m_texture = gl::Texture(loadImage(assetPath("suzanne.jpg")), true, "suzanne.jpg");
    m_meshSampler = gl::Sampler(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT, "trilinear repeat");
    m_screenSampler = gl::Sampler(GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, "linear clamp");
    m_sceneUniforms = gl::Buffer(sizeof(shading::Uniforms), nullptr, GL_DYNAMIC_STORAGE_BIT, "07 scene uniforms");
    m_postUniforms = gl::Buffer(sizeof(PostUniforms), nullptr, GL_DYNAMIC_STORAGE_BIT, "07 post uniforms");
    // The full-screen triangle has no vertex attributes, but core profile still insists on a bound VAO.
    glCreateVertexArrays(1, &m_emptyVao);
  }

  ~RenderToTextureGL() override {
    destroyTargets();
    glDeleteVertexArrays(1, &m_emptyVao);
  }

  void setupCamera(Camera& camera) override { camera.setHome(kCameraHome); }

  void update(float /*dt*/, Frame& frame) override {
    m_sceneProgram.reloadIfChanged();
    m_postProgram.reloadIfChanged();
    // The offscreen targets follow the window size (the original created them once and never resized).
    if (frame.framebufferSize != m_targetSize) createTargets(frame.framebufferSize);

    m_sceneUniforms.update(shading::makeUniforms(frame.camera.projection(frame.aspect(), ClipDepth::NegOneToOne),
                                                 frame.camera.view(), glm::mat4(1.0f), m_params.light,
                                                 m_params.material));
    m_postUniforms.update(PostUniforms{
        glm::vec4(float(frame.time), m_params.wobble, 0.0f, 0.0f),
        glm::vec4(frame.camera.nearZ, frame.camera.farZ, m_params.depthRange, m_params.showDepth ? 1.0f : 0.0f)});
  }

  void render(const Frame& frame) override {
    // --- pass 1: scene -> offscreen framebuffer ----------------------------------------------------------
    // VK: vkCmdBeginRendering with the offscreen color + depth image views as attachments.
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_targetSize.x, m_targetSize.y);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);

    m_sceneProgram.use();
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_sceneUniforms.id());
    m_texture.bind(1);  // unit 1: the shared shader's SAMPLER(0, 1) (VK: set 0, binding 1)
    m_meshSampler.bind(1);
    m_mesh.draw();

    // --- pass 2: offscreen textures -> screen ------------------------------------------------------------
    // Pass 1 wrote the textures, pass 2 reads them. GL tracks that hazard and makes pass 2 wait.
    // VK: *you* record an image barrier between the two passes (color/depth attachment write ->
    // fragment shader read, plus a layout transition to SHADER_READ_ONLY_OPTIMAL). Forget it and you get
    // garbage or a validation error.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, frame.framebufferSize.x, frame.framebufferSize.y);
    glDisable(GL_DEPTH_TEST);  // one triangle covering the screen: nothing to depth-test
    glDisable(GL_CULL_FACE);

    m_postProgram.use();
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, m_postUniforms.id());
    glBindTextureUnit(0, m_colorTexture);
    glBindTextureUnit(1, m_depthTexture);
    m_screenSampler.bind(0);
    m_screenSampler.bind(1);
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindSampler(0, 0);
    glBindSampler(1, 0);
  }

  void ui() override { paramsUi(m_params); }

 private:
  void createTargets(glm::ivec2 size) {
    destroyTargets();
    m_targetSize = size;

    // Color and depth *textures* (not renderbuffers), because pass 2 samples both.
    // VK: two VkImages with usage COLOR_ATTACHMENT|SAMPLED and DEPTH_STENCIL_ATTACHMENT|SAMPLED, memory, views.
    glCreateTextures(GL_TEXTURE_2D, 1, &m_colorTexture);
    glTextureStorage2D(m_colorTexture, 1, GL_RGBA8, size.x, size.y);
    glObjectLabel(GL_TEXTURE, m_colorTexture, -1, "07 offscreen color");
    glCreateTextures(GL_TEXTURE_2D, 1, &m_depthTexture);
    glTextureStorage2D(m_depthTexture, 1, GL_DEPTH_COMPONENT32F, size.x, size.y);
    glObjectLabel(GL_TEXTURE, m_depthTexture, -1, "07 offscreen depth");

    // The framebuffer object ties them together. VK (1.3, dynamic rendering) has no such object: the
    // attachments are listed in VkRenderingInfo each time you begin rendering. (Legacy VK: VkFramebuffer +
    // VkRenderPass, both created up front.)
    glCreateFramebuffers(1, &m_fbo);
    glNamedFramebufferTexture(m_fbo, GL_COLOR_ATTACHMENT0, m_colorTexture, 0);
    glNamedFramebufferTexture(m_fbo, GL_DEPTH_ATTACHMENT, m_depthTexture, 0);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;  // fragment output 0 -> color attachment 0
    glNamedFramebufferDrawBuffers(m_fbo, 1, &drawBuffer);
    glObjectLabel(GL_FRAMEBUFFER, m_fbo, -1, "07 offscreen fbo");

    // GL checks the combination only when asked (or at draw time). VK validates attachments up front
    // (validation layers) — there is no "incomplete framebuffer" state.
    const GLenum status = glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) throw std::runtime_error(fmt::format("fbo incomplete: 0x{:x}", status));
  }

  void destroyTargets() {
    glDeleteFramebuffers(1, &m_fbo);
    glDeleteTextures(1, &m_colorTexture);
    glDeleteTextures(1, &m_depthTexture);
    m_fbo = m_colorTexture = m_depthTexture = 0;
  }

  Params m_params;
  gl::Program m_sceneProgram, m_postProgram;
  gl::Mesh m_mesh;
  gl::Texture m_texture;
  gl::Sampler m_meshSampler, m_screenSampler;
  gl::Buffer m_sceneUniforms, m_postUniforms;
  GLuint m_emptyVao = 0;

  glm::ivec2 m_targetSize{0};
  GLuint m_fbo = 0;
  GLuint m_colorTexture = 0;
  GLuint m_depthTexture = 0;
};

GLINT_REGISTER_GL("07_render_to_texture", RenderToTextureGL);

}  // namespace glint::render_to_texture
