# 07_render_to_texture

## What it shows
Two passes. Pass 1 renders the lit Suzanne into offscreen **color + depth textures**. Pass 2 draws a
full-screen triangle that samples them with an animated "wobble" distortion, or shows the depth buffer
as linear distance.

## GL path
Framebuffers are written **raw** here. `gl::Framebuffer` comes after this and is used by 08.
- The offscreen targets are `GL_RGBA8` and `GL_DEPTH_COMPONENT32F` textures (textures rather than
  renderbuffers, because pass 2 samples both). They're tied together by `glCreateFramebuffers` +
  `glNamedFramebufferTexture` + `glNamedFramebufferDrawBuffers`, then checked with
  `glCheckNamedFramebufferStatus`.
- The targets are recreated when the window size changes.
- Pass 2 uses no vertex buffer: `glDrawArrays(GL_TRIANGLES, 0, 3)`, with positions derived from
  `gl_VertexID`. An empty VAO still has to be bound, because core profile requires one.
- The post-process pass has its own small std140 block (`PostUniforms`) at binding point 1.

## VK path
- **Offscreen targets.** `vk::Image` color (`RGBA8`, `COLOR_ATTACHMENT | SAMPLED`) and depth
  (`D32_SFLOAT`, `DEPTH_STENCIL_ATTACHMENT | SAMPLED`), recreated on resize. There's no framebuffer
  object: pass 1 lists them in `vkCmdBeginRendering`, with depth `storeOp = STORE` because pass 2 reads it.
- **Pipelines.** The scene pipeline is the standard shading shader, but with color format `RGBA8` (not the
  swapchain's). The post pipeline has an *empty* vertex input state and draws with `vkCmdDraw(3)`.
- **Four barriers per frame:**
  - **Before pass 1**: both images `UNDEFINED → attachment`. `src` is the *previous frame's pass 2*
    (fragment-shader reads, so write-after-read, an execution dependency only), because both frames in flight
    share the images.
  - **Between the passes**: color `COLOR_ATTACHMENT_OUTPUT` write and depth fragment-test write →
    `FRAGMENT_SHADER` read, layouts → `SHADER_READ_ONLY_OPTIMAL`. This is exactly what GL hides.
- **The shader differs from GL in two places.** It flips `v` before sampling, because VK render targets
  store the top row first. Its depth linearisation also differs: VK stores NDC z directly, so there's no
  `d*2-1` and the inverse of `perspectiveRH_ZO` is used.
- Depth is sampled with a *nearest* sampler. Linear filtering of depth formats is optional in VK.

## Differences that matter
- **No framebuffer object in VK 1.3.** Attachments are listed in `VkRenderingInfo` at every
  `vkCmdBeginRendering`. Legacy VK had `VkRenderPass` + `VkFramebuffer`, both created up front.
- **The hazard between the passes.** GL sees that pass 2 samples what pass 1 rendered and orders them
  itself. VK needs an explicit image barrier (attachment write → fragment shader read) with a layout
  transition to `SHADER_READ_ONLY_OPTIMAL`. **This is the technique where GL's implicit synchronisation
  becomes visible.**
- **Depth linearisation.** GL stores `d = z_ndc * 0.5 + 0.5`, so the shader first undoes that. VK stores
  NDC z directly (already [0, 1]), so that step disappears.
- `gl_VertexID` is `gl_VertexIndex` in VK GLSL.

## Gotchas hit
- 🔴 **Sync validation: `READ_AFTER_WRITE` on both offscreen images** in pass 2, although the barrier was
  there. The barrier's `dstAccessMask` was `SHADER_SAMPLED_READ`, and the layer tracks descriptor image
  reads as generic shader reads (it reported `SHADER_STORAGE_READ`). With `SHADER_READ`, which includes
  sampled reads, the hazards are gone. This is now the rule for every "read it in a shader next" barrier
  (see `vk/barrier.h`), including 03's upload.
- Without the `v` flip, the VK output is upside down. The flipped viewport makes NDC match GL, but memory
  rows are still top-down.
Fixed from Glint_gl (known-issues.md):
- `time` and the depth uniforms were never set, so the wobble was frozen and the depth view showed raw
  depth in the red channel. They now come from the frame time and the camera's near/far.
- The FBO kept its initial window size; it now follows the window.
- The depth view needed a linear range (white at the near plane, black at N units away). Mapping the
  full near..far range made Suzanne a flat gray.

## Numbers
LOC: gl.cpp 150, vk.cpp 218.
