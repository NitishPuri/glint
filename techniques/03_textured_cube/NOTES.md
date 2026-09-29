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
*(Phase 5.)*

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
- ImGui's font atlas also lives on texture unit 0. The technique unbinds its sampler after drawing so it
  doesn't change how ImGui's font is filtered.
- Glint_gl never bound the texture in `onRender` and relied on the constructor leaving it bound. Here the
  texture is bound explicitly for every draw.

## Numbers
LOC: gl.cpp 107. VK: tbd.
