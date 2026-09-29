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
*(Phase 5.)*

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
Fixed from Glint_gl (known-issues.md):
- `time` and the depth uniforms were never set, so the wobble was frozen and the depth view showed raw
  depth in the red channel. They now come from the frame time and the camera's near/far.
- The FBO kept its initial window size; it now follows the window.
- The depth view needed a linear range (white at the near plane, black at N units away). Mapping the
  full near..far range made Suzanne a flat gray.

## Numbers
LOC: gl.cpp 150. VK: tbd.
