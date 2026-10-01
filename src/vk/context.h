#pragma once

// Everything GL gives you implicitly with "make the context current": instance, validation, surface,
// physical device choice, logical device and queue. Created once per app run.
//
// Kept as a plain struct of handles on purpose: techniques use the raw handles directly
// (ctx.device, ctx.physicalDevice, ...), the way a vk.cpp next to a gl.cpp should read.

#include <string>

#include "vk/vk.h"

struct GLFWwindow;

namespace glint::vk {

struct ContextDesc {
  GLFWwindow* window = nullptr;
  bool validation = true;  // VK_LAYER_KHRONOS_validation + synchronization validation
};

class Context {
 public:
  explicit Context(const ContextDesc& desc);
  ~Context();
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

  VkInstance instance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties properties{};
  VkPhysicalDeviceMemoryProperties memoryProperties{};
  VkDevice device = VK_NULL_HANDLE;
  uint32_t queueFamily = 0;  // one family for graphics + present (true on every desktop GPU we target)
  VkQueue queue = VK_NULL_HANDLE;
  bool pipelineStatistics = false;  // the pipelineStatisticsQuery feature is enabled

  // Index of a memory type allowed by `typeBits` (from vkGet*MemoryRequirements) that has all `flags`.
  // Throws if there is none. (01_triangle spells this loop out once; everything else calls this.)
  uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags flags) const;

  // "AMD Radeon Graphics (RADV RENOIR), driver Mesa 25.1.5, Vulkan 1.4.311"
  std::string deviceDescription() const;

  // Validation messages of severity warning or error so far. "Validation clean" means this stays 0.
  static int validationIssueCount();
};

}  // namespace glint::vk
