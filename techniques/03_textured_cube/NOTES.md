# 03_textured_cube

## What it shows
Texturing: an image loaded on the CPU (`core::loadImage`, RGBA8, flipped so uv (0,0) is the bottom-left),
uploaded with a full mip chain, and read through a sampler. From `UVCubeScene`. The UI switches between
`box.jpg` and `grid.png` and between nearest, linear and trilinear filtering; nearest on `grid.png` shows
the texels most clearly.

## GL path
Textures are written **raw** here. The `gl::Texture` / `gl::Sampler` helpers come after this.
- `glCreateTextures` + `glTextureStorage2D(levels, GL_RGBA8)` allocate immutable storage for all mip levels.
- `glTextureSubImage2D` uploads level 0, then `glGenerateTextureMipmap` fills the rest.
- A sampler object (`glCreateSamplers` + `glSamplerParameteri`) holds wrap and filter settings. It's bound
  next to the texture: `glBindTextureUnit(0, tex)` + `glBindSampler(0, sampler)`, read by
  `layout(binding = 0) uniform sampler2D`.
- Switching textures deletes and recreates the texture, because immutable storage can't change size.

## VK path
Textures are written **raw**. `vk::createTexture` / `vk::createSampler` come after this. Buffers, depth, mesh and
pipeline use the helpers extracted after 02.
- **Upload + mips** (one `OneTimeCommands`):
  1. All levels `UNDEFINED → TRANSFER_DST`.
  2. `vkCmdCopyBufferToImage` from a staging buffer into level 0.
  3. For each level *i*: level *i-1* `TRANSFER_DST → TRANSFER_SRC`, then `vkCmdBlitImage` (i-1 → i, half size,
     linear filter).
  4. Levels 0..n-2 `TRANSFER_SRC → SHADER_READ_ONLY`, and the last level `TRANSFER_DST → SHADER_READ_ONLY`.
     Two barriers, because the levels are in two different layouts.

  First it checks that RGBA8 supports `SAMPLED_IMAGE_FILTER_LINEAR` (it's needed for linear blits).
- **Samplers.** Three immutable `VkSampler`s (nearest, linear, trilinear). "Linear/nearest without mips" is
  `maxLod = 0`.
- **Descriptors.** Binding 0 is the uniform buffer (vertex stage) and binding 1 is a combined image sampler
  (fragment stage). There's one set per frame slot. Binding 1 is **rewritten every frame** with the current
  texture view + sampler. That's legal because this slot's fence was waited on, so no pending work uses the set.
- **Switching textures** waits for the GPU to go idle before destroying the old image, because frames in
  flight may still sample it.

## Differences that matter
- **Upload.** GL copies straight from your pointer. VK needs a staging buffer, `vkCmdCopyBufferToImage`,
  and layout transitions (UNDEFINED → TRANSFER_DST → SHADER_READ_ONLY) with barriers.
- **Mipmaps.** `glGenerateTextureMipmap` is one call. VK has none: you run a loop of `vkCmdBlitImage` +
  barriers, one per level, after checking that the format supports linear blits.
- **Samplers.** Both have separate sampler objects, but a GL sampler is mutable (this technique changes
  its filter every frame), while a `VkSampler` is immutable, so you'd create one per mode.
- **Binding.** In GL, texture units are global slots. In VK, the image view and sampler are written into a
  descriptor set, which is bound per draw.

## Gotchas hit
- 🔴 **Synchronization validation caught a real hazard** (`SYNC-HAZARD-WRITE_AFTER_WRITE` on `vkCmdBlitImage`).
  The first barrier moved *all* levels to `TRANSFER_DST`, but its `dstStageMask` was only `COPY`. The blits
  write levels 1..n in the `BLIT` stage, which wasn't ordered after that layout transition. The fix is
  `dst = COPY | BLIT`. In GL this bug can't exist, because `glGenerateTextureMipmap` does all of it. It
  also renders correctly on RADV either way, which is why you need the validation layer to find it.
- A pipeline that declared normal and tangent inputs the shader doesn't read got a performance warning
  ("vertex attribute not consumed"). `vk::Mesh::vertexInput({kAttribPosition, kAttribUv})` now declares
  only what the shader reads.
- The per-technique summary said "0 validation issues" although the errors above had happened. Its
  baseline was taken *after* the first technique's `init()`. Both apps now take it before.
- ImGui's font atlas also lives on texture unit 0. The technique unbinds its sampler after drawing so it
  doesn't change how ImGui's font is filtered.
- Glint_gl never bound the texture in `onRender` and relied on the constructor leaving it bound. Here the
  texture is bound explicitly for every draw.

## Numbers
LOC: gl.cpp 107, vk.cpp 324.
