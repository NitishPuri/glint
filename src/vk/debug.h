#pragma once

// VK_EXT_debug_utils: object names and command-buffer labels, visible in validation messages and RenderDoc.
// GL equivalents: glObjectLabel, glPushDebugGroup/glPopDebugGroup.
// Extension functions aren't exported by the loader library, so they're fetched once with
// vkGetInstanceProcAddr (load()); the helpers are no-ops if the extension isn't available.

#include <string_view>

#include "vk/vk.h"

namespace glint::vk::debug {

void load(VkInstance instance);

void setName(VkDevice device, VkObjectType type, uint64_t handle, std::string_view name);

template <typename Handle>
void setName(VkDevice device, VkObjectType type, Handle handle, std::string_view name) {
  setName(device, type, reinterpret_cast<uint64_t>(handle), name);
}

void beginLabel(VkCommandBuffer cmd, std::string_view name);
void endLabel(VkCommandBuffer cmd);

// RAII label: { vk::debug::Label label(cmd, "shadow pass"); ... }
struct Label {
  Label(VkCommandBuffer cmd, std::string_view name) : m_cmd(cmd) { beginLabel(cmd, name); }
  ~Label() { endLabel(m_cmd); }
  Label(const Label&) = delete;
  Label& operator=(const Label&) = delete;

 private:
  VkCommandBuffer m_cmd;
};

}  // namespace glint::vk::debug
