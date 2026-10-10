// 08_shadow_mapping — OpenGL. Pass 1 renders depth from the light into a depth-only framebuffer; pass 2
// renders the scene from the camera and compares each fragment's light-space depth against that map.
// First technique on gl::Framebuffer (07 did framebuffers raw).

#include <imgui.h>

#include "core/assets.h"
#include "core/camera.h"
#include "gl/buffer.h"
#include "gl/framebuffer.h"
#include "gl/gl.h"
#include "gl/mesh.h"
#include "gl/shader.h"
#include "gl/technique.h"
#include "gl/texture.h"
#include "params.h"

namespace glint::shadow_mapping {

class ShadowMappingGL final : public gl::Technique {
 public:
  void init() override {
    m_depthProgram = gl::Program({"08_shadow_mapping/shaders/depth.gl.vert"});  // vertex stage only
    m_shadowProgram =
        gl::Program({"08_shadow_mapping/shaders/shadow.gl.vert", "08_shadow_mapping/shaders/shadow.gl.frag"});
    MeshData room = loadObj(assetPath("room/room_thickwalls.obj"));
    indexMesh(room);
    m_mesh = gl::Mesh(room, "room");
    m_texture = gl::Texture(loadImage(assetPath("room/uvmap.jpg")), true, "room uvmap");
    m_sampler = gl::Sampler(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT, "trilinear repeat");
    m_uniforms = gl::Buffer(sizeof(Uniforms), nullptr, GL_DYNAMIC_STORAGE_BIT, "08 uniforms");

    // The comparison lives in the *sampler*, not the texture: the same depth texture is also shown as a
    // plain grayscale image in the UI, which needs compare mode off. VK does it the same way.
    m_shadowSampler = gl::Sampler(GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_BORDER, "shadow compare");
    m_shadowSampler.set(GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    m_shadowSampler.set(GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    // Outside the light's frustum: depth 1 = "nothing in front" = lit.
    const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glSamplerParameterfv(m_shadowSampler.id(), GL_TEXTURE_BORDER_COLOR, border);

    createShadowMap();
  }

  void setupCamera(Camera& camera) override { camera.setHome(kCameraHome); }

  void update(float /*dt*/, Frame& frame) override {
    m_depthProgram.reloadIfChanged();
    m_shadowProgram.reloadIfChanged();

    m_lightViewProj = lightViewProjection(m_params, ClipDepth::NegOneToOne);
    // Light clip space -> shadow map lookup: x, y from [-1,1] to texture coords [0,1], and z from [-1,1] to
    // the [0,1] the depth buffer stores. VK: z is already [0,1], so only x and y get remapped there.
    const glm::mat4 bias(0.5f, 0.0f, 0.0f, 0.0f,  //
                         0.0f, 0.5f, 0.0f, 0.0f,  //
                         0.0f, 0.0f, 0.5f, 0.0f,  //
                         0.5f, 0.5f, 0.5f, 1.0f);
    const glm::mat4 model(1.0f);
    const glm::mat4 view = frame.camera.view();
    m_uniforms.update(Uniforms{
        frame.camera.projection(frame.aspect(), ClipDepth::NegOneToOne) * view * model, view, model,
        bias * m_lightViewProj, glm::vec4(glm::normalize(m_params.lightPosition), 0.0f),
        glm::vec4(m_params.lightColor, m_params.lightPower),
        glm::vec4(m_params.ambient, m_params.specular, m_params.bias, m_params.pcf ? 1.0f : 0.0f)});
  }

  void render(const Frame& frame) override {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // --- pass 1: depth from the light -----------------------------------------------------------------
    // Its own resolution, independent of the window (the original tied it to the window size).
    m_shadowFbo.bind();
    glViewport(0, 0, m_shadowMap.width(), m_shadowMap.height());
    glClear(GL_DEPTH_BUFFER_BIT);
    m_depthProgram.use();
    m_depthProgram.set(0, m_lightViewProj);  // model = identity
    m_mesh.draw();

    // --- pass 2: camera view, sampling the shadow map ---------------------------------------------------
    // GL notices that pass 2 reads what pass 1 wrote and orders them. VK: a barrier on the shadow map
    // (depth attachment write -> fragment shader read, layout DEPTH_ATTACHMENT -> SHADER_READ_ONLY).
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, frame.framebufferSize.x, frame.framebufferSize.y);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_shadowProgram.use();
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_uniforms.id());
    m_texture.bind(0);
    m_sampler.bind(0);
    m_shadowMap.bind(1);
    m_shadowSampler.bind(1);
    m_mesh.draw();

    // Leave no half-state behind: with the comparison sampler unbound but the depth texture still on unit 1
    // and this program still current, the *next* draw (ImGui's) would see "depth texture + no compare mode +
    // sampler2DShadow" — undefined behaviour that NVIDIA's driver reports and Mesa doesn't (found 2026-10-10).
    glBindSampler(0, 0);
    glBindSampler(1, 0);
    glBindTextureUnit(1, 0);
    glUseProgram(0);
  }

  void ui() override {
    if (paramsUi(m_params)) createShadowMap();
    if (m_params.showShadowMap) {
      // ImGui's GL backend takes a GL texture name as ImTextureID. v flipped: GL textures start at the
      // bottom row, ImGui draws top-down. (VK: ImGui_ImplVulkan_AddTexture(sampler, view, layout).)
      const float w = ImGui::GetContentRegionAvail().x;
      ImGui::Image(ImTextureID(m_shadowMap.id()), ImVec2(w, w), ImVec2(0, 1), ImVec2(1, 0));
    }
  }

 private:
  void createShadowMap() {
    const int size = kShadowMapSizes[m_params.shadowMapSize];
    m_shadowMap = gl::Texture(GL_DEPTH_COMPONENT32F, size, size, 1, "shadow map");
    // Parameters on the texture itself are what gets used when *no* sampler object is bound — i.e. by
    // ImGui when it draws the preview. Linear, no mips, no compare; and show depth as gray instead of red.
    // VK: the swizzle is VkImageViewCreateInfo::components, set on a separate view for the UI.
    glTextureParameteri(m_shadowMap.id(), GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_shadowMap.id(), GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    const GLint swizzle[4] = {GL_RED, GL_RED, GL_RED, GL_ONE};
    glTextureParameteriv(m_shadowMap.id(), GL_TEXTURE_SWIZZLE_RGBA, swizzle);

    m_shadowFbo = gl::Framebuffer("shadow fbo");
    m_shadowFbo.attachDepth(m_shadowMap);
    m_shadowFbo.noColor();
    m_shadowFbo.checkComplete();
  }

  Params m_params;
  gl::Program m_depthProgram, m_shadowProgram;
  gl::Mesh m_mesh;
  gl::Texture m_texture;
  gl::Sampler m_sampler;
  gl::Buffer m_uniforms;
  gl::Texture m_shadowMap;
  gl::Sampler m_shadowSampler;
  gl::Framebuffer m_shadowFbo;
  glm::mat4 m_lightViewProj{1.0f};
};

GLINT_REGISTER_GL("08_shadow_mapping", ShadowMappingGL);

}  // namespace glint::shadow_mapping
