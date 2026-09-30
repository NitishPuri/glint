#pragma once

// SPIR-V loading, as 01_triangle does it raw. Paths are relative to GLINT_SHADER_DIR (build/<cfg>/shaders),
// where cmake/shaders.cmake puts `techniques/<t>/shaders/foo.vk.vert` as `<t>/foo.vert.spv`.

#include <string>

#include "vk/vk.h"

namespace glint::vk {

// Throws if the file is missing (not built?). The caller destroys the module (usually right after
// creating the pipeline).
VkShaderModule loadShaderModule(VkDevice device, const std::string& relativePath);

}  // namespace glint::vk
