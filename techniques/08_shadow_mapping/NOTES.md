# 08_shadow_mapping

## What it shows
Classic two-pass shadow mapping. Pass 1 renders depth from the light, using an orthographic frustum for
a directional light or a perspective one for the "spotlight". Pass 2 renders from the camera and compares
each fragment's light-space depth with the map, with 4-tap Poisson PCF or a single tap. Both use the
hardware's 2×2 compare filtering. The shadow map can be shown in the panel.

## GL path
- Pass 1:
  - Depth-only: a `gl::Framebuffer` with a `GL_DEPTH_COMPONENT32F` texture and `glNamedFramebufferDrawBuffer(GL_NONE)`.
  - The program has **only a vertex stage**, because depth is written by the fixed-function depth test.
  - The resolution is chosen independently of the window (512 to 4096).
- Pass 2:
  - `shadowMatrix = bias * lightViewProj` maps light clip space to [0, 1] texture coordinates and depth.
  - The map is read through `sampler2DShadow` with a **sampler object** set to `GL_COMPARE_REF_TO_TEXTURE`
    / `GL_LEQUAL`, `GL_LINEAR` filtering (free 2×2 PCF), and a border depth of 1, so points outside the
    map count as lit.
- Preview:
  - The same texture is drawn by `ImGui::Image`. With no sampler bound, ImGui reads the texture's *own*
    parameters: linear filtering, no compare.
  - A swizzle (R, R, R, 1) turns the red depth channel into gray.

## VK path
*(Phase 5.)*

## Differences that matter
- **Compare sampling.** GL: `GL_TEXTURE_COMPARE_MODE` on a sampler or texture. VK:
  `VkSamplerCreateInfo::compareEnable/compareOp`, plus a depth format whose features include
  `SAMPLED_IMAGE_FILTER_LINEAR` for the 2×2 PCF.
- **The bias matrix.** GL remaps x, y and z from [-1, 1] to [0, 1]. In VK z is already [0, 1] and only x
  and y are remapped. Y also needs care there because of the flipped viewport.
- **Depth-only pipeline.** VK allows a pipeline with no fragment shader and no color attachments, just
  like the vertex-only GL program. Depth bias can move into `vkCmdSetDepthBias` (GL's `glPolygonOffset`)
  instead of the shader.
- **The pass-to-pass barrier**, as in 07: depth attachment write → fragment shader read.
- **Swizzle.** GL: `GL_TEXTURE_SWIZZLE_RGBA` on the texture. VK: `VkComponentMapping` on an image view,
  so you'd create a second view for the UI.

## Gotchas hit
Fixed from Glint_gl (known-issues.md):
- 🔴 The shadow texture was sampled as `sampler2DShadow` **without compare mode**, which is undefined behaviour.
  On radeonsi it returned "in shadow" everywhere, so Glint_gl's room was lit by ambient only. From the
  same camera (5, 4, -13), glint now shows the pillar and sphere shadows on a lit floor. This is the only
  visual difference from the old scene.
- The shadow map was sized to the window and never resized. It now has its own fixed size.
- The spotlight passed `45` to `glm::perspective`, which expects radians. It also divided only `z` by `w`
  in the lookup; the shader now does the full perspective divide.
- The unused `LightPosition_worldspace` uniform that made the driver warn is gone.

## Numbers
LOC: gl.cpp 141. VK: tbd.
