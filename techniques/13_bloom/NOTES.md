# 13_bloom

## What it shows
12's HDR room with **bloom** and **color grading**:
- **Bloom**: a soft-threshold bright pass, then a blur built from many cheap small filters at low resolution. The
  HDR image is downsampled through a 6-level mip chain (½ … 1/64 of the screen), then upsampled back, adding each
  level onto the next larger one. Level 0 ends up holding a wide, smooth glow, which is added to the scene
  *before* tone mapping.
- **Grading**: saturation, contrast, temperature and gamma are baked on the CPU into a 32³ lookup table. The
  shader applies all of them with a single LUT lookup after tone mapping.

The UI has three views (final / bloom only / no bloom), the bloom knobs (threshold, knee, intensity, radius,
level count, Karis average) and the grading controls. A grading change re-bakes the LUT and re-uploads it.

## Read
- [GG1 ch21 Real-Time Glow](https://developer.nvidia.com/gpugems/gpugems/part-iv-image-processing/chapter-21-real-time-glow)
  — the glow pipeline: glow source → blur at low resolution → add. The chapter blurs with separable Gaussians
  at ¼ res; the mip-chain blur here is the modern replacement for that step (next item). Local CD: — (online only)
- Jorge Jimenez, *Next Generation Post Processing in Call of Duty: Advanced Warfare* (SIGGRAPH 2014): the 13-tap
  downsample, tent upsample and Karis average used here.
- [GG1 ch22 Color Controls](https://developer.nvidia.com/gpugems/gpugems/part-iv-image-processing/chapter-22-color-controls)
  — the operations themselves (levels, saturation via luminance, curves). Each of them is a function of the
  color alone, so all of them together can be baked into one table. Local CD:
  `GPU-Gems-1-CD-Content/Image_Processing/Color_Controls` (readme only)
- [GG2 ch24 Using Lookup Tables to Accelerate Color Transformations](https://developer.nvidia.com/gpugems/gpugems2/part-iii-high-quality-rendering/chapter-24-using-lookup-tables-accelerate-color)
  — 3D LUTs, their size/precision trade-offs, and why the lookup must address texel *centers*. Local CD: —
  (online only)

## The pass chain (identical in both APIs)
| Pass | Target | Reads | Notes |
|---|---|---|---|
| scene | HDR (RGBA16F) | — | 12's shaders, unchanged |
| down 0 | bloom level 0 | HDR | 13 taps, + Karis average + soft threshold (prefilter) |
| down i | level i | level i-1 | 13 taps |
| up i (i = L-2 … 0) | level i, **loaded**, blend ONE+ONE | level i+1 | 3×3 tent, radius in source texels |
| composite | screen | HDR, level 0, LUT | (HDR + bloom·intensity/L) → exposure → tone map → sRGB → LUT |

That's 2L+1 post passes (13 at L = 6) where 12 had one. Shaders are all shared.
`common/shaders/fullscreen.vert` (moved from 12) now does the VK v-flip once for every post pass, and
`common/shaders/color.glsl` holds 12's tone-map operators.

## GL path
- **Bloom chain**: one `GL_R11F_G11F_B10F` texture with 6 levels, plus **one texture view per level**
  (`glTextureView(view, GL_TEXTURE_2D, chain, format, minLevel = i, numLevels = 1, ...)`). Each view is used
  both as the FBO attachment (level 0 *of the view*) and as the sampled source of the next pass.
  - Views need immutable storage, and the view name must come from `glGenTextures`. A `glCreateTextures` name
    is already initialized as a texture, and `glTextureView` rejects it.
  - Sampling level i+1 while rendering into level i of the same texture is **not** a feedback loop, because the
    sampled level range excludes the attached level. The single-level views make that explicit.
- **No synchronization code at all**: GL orders "rendered into a texture, then sampled" by itself, for every one
  of the 13 passes.
- **Additive upsample**: `glEnable(GL_BLEND)`, `glBlendFunc(GL_ONE, GL_ONE)`. The existing contents are kept
  because GL never clears implicitly.
- **LUT update**: `glTextureSubImage2D` when a grading control changes. The driver keeps frames still in
  flight on the old contents.

## VK path
- **Bloom chain**: one `VkImage` (`B10G11R11_UFLOAT_PACK32`, 6 mips), plus one `VkImageView` per level
  (`subresourceRange.baseMipLevel = i, levelCount = 1`). An attachment view must cover exactly one level.
  - The format is **queried** (`vkGetPhysicalDeviceFormatProperties`: `COLOR_ATTACHMENT`,
    `COLOR_ATTACHMENT_BLEND`, `SAMPLED_IMAGE_FILTER_LINEAR`) because rendering and blending into it are optional
    in VK, with RGBA16F as the fallback. GL 4.x requires the format to be renderable.
- **Per-level layouts and barriers**: levels of one image sit in different layouts at once (level i
  `COLOR_ATTACHMENT_OPTIMAL` while level i-1 is `SHADER_READ_ONLY_OPTIMAL`). `vk::transitionImage` gained an
  optional mip range for this. The barriers per frame:
  - The whole chain goes `UNDEFINED → COLOR_ATTACHMENT`, ordered after last frame's writes and reads.
  - After each downsample, its level goes attachment → sampled.
  - Before each upsample, level i goes **`SHADER_READ_ONLY → COLOR_ATTACHMENT`**. The old layout is the real
    one, not `UNDEFINED`, because the contents must survive. The barrier is also a write→read dependency,
    because blending *reads* the earlier downsample result.
  - After each upsample, the level goes attachment → sampled again.
  - That's 2L+1 bloom barriers (≈ 13) per frame. GL has none.
- **Additive upsample**: blending is baked into the pipeline (`GraphicsPipelineDesc::additiveBlend`). The
  target is begun with `LOAD_OP_LOAD` (`RenderTarget::loadColor`), whereas every earlier technique cleared.
- **Descriptor sets**: one per pass (6 down, 5 up, 1 composite) sharing one single-image layout. They only
  change on resize, so they are written in `createTargets()`, never per frame.
- **LUT update, inside the frame's command buffer** (`uploadLut`):
  1. memcpy into this frame slot's host-visible staging buffer. It's free, because this slot's fence was
     waited on.
  2. Barrier `UNDEFINED → TRANSFER_DST`, after the fragment-shader reads of earlier submissions (an execution
     dependency, enough for write-after-read).
  3. `vkCmdCopyBufferToImage`.
  4. Barrier `TRANSFER_DST → SHADER_READ_ONLY`.

  Two staging buffers (one per frame in flight). No `vkDeviceWaitIdle`, unlike resize.

## Differences that matter
- **Mip levels as render targets**: GL texture view + FBO ≈ VK image view + `VkRenderingInfo`. The concepts
  match one-to-one; GL simply hides the layouts and barriers.
- **Load vs clear**: GL framebuffers keep their contents until you clear. In VK, each `vkCmdBeginRendering`
  states it (`loadOp`): `CLEAR`, `LOAD` (keep) or `DONT_CARE` (garbage allowed, the fastest on tilers).
- **Re-uploading a texture that is in use**: GL's driver handles it (copy or stall). In VK you record the copy
  and the barriers yourself, in the frame that needs it. Submission order on one queue plus the barrier is
  what keeps the previous frame's reads safe.
- **Format capabilities**: GL has a fixed list of required renderable formats. VK makes the app ask per format,
  per tiling, per usage.

## Gotchas hit
- With the threshold at 1, the whole lit room passes the bright pass and a red haze covers the dark floor
  edge. The default threshold is now 2 with a knee of 1: bloom is for what is *much* brighter than white.
  (With threshold 0 it becomes "physically based" bloom: everything scatters a little.)
- Mip i of a texture whose level 0 is `size >> 1` is `size >> (i+1)` (rounded down, at least 1) in both APIs.
  `levelSize()` in params.h keeps both viewports in sync with that.

## Numbers
LOC: gl.cpp 218, vk.cpp 391. Parity 61.4 dB. GPU at 1280×720 (RADV / radeonsi, Debug): GL 0.53 ms, VK 0.61 ms,
versus 12's 0.24 / 0.27. The bloom chain plus composite costs ≈ 0.3 ms. Validation is clean with synchronization
validation on, also while the LUT re-uploads every frame and the level count changes every frame (a temporary
stress hack).
