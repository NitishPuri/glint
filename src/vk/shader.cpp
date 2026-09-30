#include "vk/shader.h"

#include <fstream>
#include <stdexcept>
#include <vector>

#include "core/log.h"

namespace glint::vk {

VkShaderModule loadShaderModule(VkDevice device, const std::string& relativePath) {
  const std::string path = std::string(GLINT_SHADER_DIR) + "/" + relativePath;
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) throw std::runtime_error(fmt::format("cannot open {} (built by glslc?)", path));
  std::vector<char> code(size_t(file.tellg()));
  file.seekg(0);
  file.read(code.data(), std::streamsize(code.size()));

  VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  info.codeSize = code.size();
  info.pCode = reinterpret_cast<const uint32_t*>(code.data());
  VkShaderModule module = VK_NULL_HANDLE;
  VK_CHECK(vkCreateShaderModule(device, &info, nullptr, &module));
  return module;
}

}  // namespace glint::vk
