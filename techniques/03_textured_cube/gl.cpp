// 03_textured_cube — OpenGL. Textures written raw (the gl::Texture / gl::Sampler helpers come after this):
// immutable storage, upload, mipmaps, and a separate sampler object.

#include "core/assets.h"
#include "core/camera.h"
#include "gl/gl.h"
#include "gl/mesh.h"
#include "gl/shader.h"
#include "gl/technique.h"
#include "params.h"

namespace glint::textured_cube {

class TexturedCubeGL final : public gl::Technique {
 public:
  void init() override {
    m_program = gl::Program({"03_textured_cube/shaders/textured.gl.vert", "03_textured_cube/shaders/textured.gl.frag"});
    m_mesh = gl::Mesh(makeCube(), "cube");
    loadTexture();

    // Sampler object: *how* to read (filtering, wrapping), separate from the texture (*what* to read).
    // Pre-GL 3.3 these settings lived on the texture itself (glTexParameteri), and plenty of GL code still
    // does that. VK only has the separate form: VkSampler vs VkImage + VkImageView.
    glCreateSamplers(1, &m_sampler);
    glSamplerParameteri(m_sampler, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glSamplerParameteri(m_sampler, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glObjectLabel(GL_SAMPLER, m_sampler, -1, "03 sampler");
  }

  ~TexturedCubeGL() override {
    glDeleteSamplers(1, &m_sampler);
    glDeleteTextures(1, &m_texture);
  }

  void update(float dt, Frame& /*frame*/) override {
    m_program.reloadIfChanged();
    m_angle += m_params.rotationSpeed * dt;
  }

  void render(const Frame& frame) override {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, frame.framebufferSize.x, frame.framebufferSize.y);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);

    // Filtering can change every frame from the UI; with a sampler object that's two cheap calls.
    // VK: samplers are immutable — you'd create one VkSampler per mode and switch descriptors.
    const GLint minFilter = m_params.filter == Filter::Nearest  ? GL_NEAREST
                            : m_params.filter == Filter::Linear ? GL_LINEAR
                                                                : GL_LINEAR_MIPMAP_LINEAR;
    glSamplerParameteri(m_sampler, GL_TEXTURE_MIN_FILTER, minFilter);
    glSamplerParameteri(m_sampler, GL_TEXTURE_MAG_FILTER, m_params.filter == Filter::Nearest ? GL_NEAREST : GL_LINEAR);

    // Bind texture + sampler to unit 0, which the shader's `layout(binding = 0) uniform sampler2D` reads.
    // (Pre-DSA: glActiveTexture(GL_TEXTURE0) + glBindTexture(GL_TEXTURE_2D, tex).)
    // VK: write the image view + sampler into a descriptor set once, bind the set per draw.
    glBindTextureUnit(0, m_texture);
    glBindSampler(0, m_sampler);

    m_program.use();
    m_program.set(0, frame.camera.viewProjection(frame.aspect(), ClipDepth::NegOneToOne) * model(m_angle));
    m_mesh.draw();

    glBindSampler(0, 0);  // unit 0 is shared with ImGui's font texture; don't leave our sampler on it
  }

  void ui() override {
    if (paramsUi(m_params)) loadTexture();
  }

 private:
  void loadTexture() {
    const ImageData image = loadImage(assetPath(kTextures[m_params.texture]));
    glDeleteTextures(1, &m_texture);  // immutable storage can't be resized: replace the whole texture

    // Full mip chain: floor(log2(max(w, h))) + 1 levels.
    int levels = 1;
    for (int size = std::max(image.width, image.height); size > 1; size /= 2) ++levels;

    // glTextureStorage2D allocates all levels at once with a fixed format and size ("immutable storage",
    // GL 4.2), like vkCreateImage + memory. The older glTexImage2D allocated one level at a time and could
    // be redefined at any moment, which drivers had to cope with.
    glCreateTextures(GL_TEXTURE_2D, 1, &m_texture);
    glTextureStorage2D(m_texture, levels, GL_RGBA8, image.width, image.height);
    // Upload level 0. The driver copies from our pointer before returning.
    // VK: copy into a host-visible staging buffer, then vkCmdCopyBufferToImage with layout transitions.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(m_texture, 0, 0, 0, image.width, image.height, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
    // Fill levels 1..n by downsampling. VK has no such call: a loop of vkCmdBlitImage + barriers per level.
    glGenerateTextureMipmap(m_texture);
    glObjectLabel(GL_TEXTURE, m_texture, -1, kTextures[m_params.texture]);
  }

  Params m_params;
  float m_angle = 0.0f;
  gl::Program m_program;
  gl::Mesh m_mesh;
  GLuint m_texture = 0;
  GLuint m_sampler = 0;
};

GLINT_REGISTER_GL("03_textured_cube", TexturedCubeGL);

}  // namespace glint::textured_cube
