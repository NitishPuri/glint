#include "vk/debug.h"

#include <string>

namespace glint::vk::debug {

namespace {
PFN_vkSetDebugUtilsObjectNameEXT g_setName = nullptr;
PFN_vkCmdBeginDebugUtilsLabelEXT g_beginLabel = nullptr;
PFN_vkCmdEndDebugUtilsLabelEXT g_endLabel = nullptr;
}  // namespace

void load(VkInstance instance) {
  g_setName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
      vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT"));
  g_beginLabel = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
      vkGetInstanceProcAddr(instance, "vkCmdBeginDebugUtilsLabelEXT"));
  g_endLabel =
      reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(vkGetInstanceProcAddr(instance, "vkCmdEndDebugUtilsLabelEXT"));
}

void setName(VkDevice device, VkObjectType type, uint64_t handle, std::string_view name) {
  if (!g_setName || handle == 0) return;
  const std::string text(name);  // needs a NUL terminator
  VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
  info.objectType = type;
  info.objectHandle = handle;
  info.pObjectName = text.c_str();
  g_setName(device, &info);
}

void beginLabel(VkCommandBuffer cmd, std::string_view name) {
  if (!g_beginLabel) return;
  const std::string text(name);
  VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
  label.pLabelName = text.c_str();
  g_beginLabel(cmd, &label);
}

void endLabel(VkCommandBuffer cmd) {
  if (g_endLabel) g_endLabel(cmd);
}

}  // namespace glint::vk::debug
