# 12_hdr_tonemapping

## What it shows

Linear-space lighting into a float target, then exposure + tone mapping + sRGB encoding. A room lit by four
point lights with intensities from 4 to 100 (1/d² falloff), so radiance near the lights is far above 1. The UI
switches tone-mapping operators (clamp, Reinhard, ACES fitted), sets exposure in EV stops, and has a
"gamma-space" toggle that reproduces what 01–11 do, for comparison. It's the first technique written
entirely with **shared shaders**: one `.vert`/`.frag` per stage for both APIs.

## Read

- [GG3 ch24 The Importance of Being Linear](https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-24-importance-being-linear)
  — the whole chapter: why lighting on sRGB-encoded values is wrong, sRGB textures, framebuffers, and how
  mipmapping and blending go wrong in gamma space; local CD: — (online only)
- Krzysztof Narkowicz, "ACES Filmic Tone Mapping Curve" (2016): the fitted curve used here.
  https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/

## Shared shaders (the Stage 8 infra step)

- `techniques/common/shaders/glint.glsl` maps four macros per API. glslc predefines `VULKAN` (= 100) when it
  targets Vulkan; GL's loader doesn't:
  - `UBO(set, binding)`, `SAMPLER(set, binding)`: VK `layout(set, binding)`, GL `layout(binding)`. GL has no
    sets, so binding numbers must be unique per resource type across sets.
  - `PUSH_CONSTANTS`: VK `layout(push_constant)`, GL a uniform block at binding point 15 that the app rewrites
    per draw. Blocks use only `mat4`/`vec4`, so std140 and the VK push-constant rules agree.
  - `VERTEX_ID`: `gl_VertexIndex` / `gl_VertexID`.
- `gl::Program` resolves `#include` itself (GL's compiler has none), with `#line` directives so errors point
  at the right file and line, and hot-reloads included files. glslc resolves includes with `-I`.
- The common standard-shading shader (04, 05, 07) moved to this scheme. Parity is unchanged, but GL's diffuse
  texture moved from unit 0 to unit 1 to match VK's binding 1.
- Per-API code stays explicit with `#ifdef VULKAN`: the full-screen vertex shader flips v for VK's top-row-first
  images. (It moved to `common/shaders/fullscreen.vert` in 13, and the tone-map operators moved to
  `common/shaders/color.glsl`.)

## GL path

- **Albedo**: `GL_SRGB8_ALPHA8` storage (`gl::Texture(..., srgb = true)`), so `texture()` decodes to linear and
  filters _after_ decoding. `glGenerateTextureMipmap` averages in linear space on sRGB textures.
- **Pass 1**: an FBO with a `GL_RGBA16F` color texture + `GL_DEPTH_COMPONENT32F`. The scene UBO is at binding 0,
  albedo at unit 1, and the per-draw block (model + emissive) at binding 15, rewritten before each of the 5
  draws (room + 4 light markers).
- **Pass 2**: the default framebuffer, an empty VAO, `glDrawArrays(3)`. The HDR texture is at unit 0 and the
  tone-map constants go in the same binding-15 block.

## VK path

- **Albedo**: `VK_FORMAT_R8G8B8A8_SRGB` (`vk::createTexture(..., srgb = true)`). The mip blits between sRGB
  levels also filter in linear space.
- **Pass 1**: an `R16G16B16A16_SFLOAT` image (`COLOR_ATTACHMENT | SAMPLED`) + D32 depth, and a scene pipeline whose
  color format is the float format, not the swapchain's. Per-draw data is `vkCmdPushConstants` (80 bytes,
  vertex + fragment stages).
- **Barriers**: HDR `UNDEFINED → COLOR_ATTACHMENT` after the previous frame's tone-map reads, then
  `COLOR_ATTACHMENT → SHADER_READ_ONLY` between the passes (as in 07).
- **Pass 2**: one descriptor set with the HDR image (rewritten on resize), push constants for the tone-map
  parameters, and a pipeline with an empty vertex input state.

## Differences that matter

- **sRGB decode** is a property of the texture's _format_ in both APIs (`GL_SRGB8_ALPHA8` /
  `VK_FORMAT_R8G8B8A8_SRGB`), so the shader code is identical.
- **sRGB encode on output**: GL enables it per framebuffer with `GL_FRAMEBUFFER_SRGB` (if the default
  framebuffer is sRGB-capable). VK makes it a property of the swapchain _format_ (`B8G8R8A8_SRGB`), or of an
  image view's format with `VK_KHR_swapchain_mutable_format`. Glint keeps one UNORM swapchain so GL and VK
  stay comparable, so the tone-map shader encodes manually.
- **Push constants**: VK has them (128 bytes guaranteed). GL emulates them with a small UBO rewritten per
  draw, which is legal only because GL snapshots buffer updates per draw (see 05).
- **Float render targets** are the same idea in both: an `RGBA16F` texture or image used as an attachment, then
  sampled.

## Gotchas hit

- glslc already defines `VULKAN`, so passing `-DVULKAN` fails with "Macro redefined".
- Killing the app with `pkill` lost the "reloaded" log lines, because stdout is buffered when redirected.
  Tests now read the log file, which is flushed per line.
- With the linear workflow off and no tone mapping (the 01–11 pipeline), the brightest light's pool is a flat
  yellow blob and the falloff looks too steep. Turning on linear lighting fixes the falloff; tone mapping then
  fixes the clipping. The four variants are compared in the vault's Stage 8 note.

## Numbers

LOC: gl.cpp 126, vk.cpp 183. Parity 62.2 dB. GPU 0.28 ms for both passes at 1000×800 (RADV, Debug).
