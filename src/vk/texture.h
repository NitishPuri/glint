#pragma once

// Textures, as 03_textured_cube does it raw: staging copy into level 0, mip chain via vkCmdBlitImage with
// per-level layout transitions, final layout SHADER_READ_ONLY_OPTIMAL. Plus sampler creation.
// GL equivalent: gl::Texture(ImageData) (glTextureStorage2D + glTextureSubImage2D + glGenerateTextureMipmap)
// and gl::Sampler.

#include <string_view>

#include "core/assets.h"
#include "vk/image.h"

namespace glint::vk {

// RGBA8 image in SHADER_READ_ONLY_OPTIMAL layout, with a full mip chain if `mipmaps`. `srgb`:
// VK_FORMAT_R8G8B8A8_SRGB, so sampling decodes sRGB -> linear (and the mip blits filter in linear space).
// Blocks until the upload is done.
Image createTexture(const Context& ctx, const ImageData& image, bool mipmaps = true, std::string_view name = {},
                    bool srgb = false);

struct SamplerDesc {
  VkFilter filter = VK_FILTER_LINEAR;
  bool mipmaps = true;  // trilinear between levels; false: level 0 only
  VkSamplerAddressMode address = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  bool compare = false;  // depth comparison (sampler2DShadow): LESS_OR_EQUAL, like GL's GL_COMPARE_REF_TO_TEXTURE
  VkBorderColor border = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;  // used with CLAMP_TO_BORDER
  std::string_view name;
};

// The caller destroys it with vkDestroySampler.
VkSampler createSampler(VkDevice device, const SamplerDesc& desc);

}  // namespace glint::vk
