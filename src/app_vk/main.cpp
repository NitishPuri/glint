// glint_vk — Phase 1 skeleton: proves the VK-side dependencies compile and link.
// The real app (instance, device, swapchain, frames in flight, ImGui, technique registry) arrives in Phase 4.

#include <GLFW/glfw3.h>
#include <fmt/core.h>
#include <imgui.h>
#include <vulkan/vulkan.h>

int main() {
  // VK: unlike GL there is no context to query — the loader can report its instance version up front.
  uint32_t instanceVersion = 0;
  vkEnumerateInstanceVersion(&instanceVersion);
  fmt::print("glint_vk skeleton\n  GLFW  {}\n  ImGui {}\n  Vulkan loader {}.{}.{} (headers {}.{}.{})\n",
             glfwGetVersionString(), IMGUI_VERSION, VK_API_VERSION_MAJOR(instanceVersion),
             VK_API_VERSION_MINOR(instanceVersion), VK_API_VERSION_PATCH(instanceVersion),
             VK_API_VERSION_MAJOR(VK_HEADER_VERSION_COMPLETE), VK_API_VERSION_MINOR(VK_HEADER_VERSION_COMPLETE),
             VK_API_VERSION_PATCH(VK_HEADER_VERSION_COMPLETE));
  fmt::print("  assets  {}\n  shaders {}\n", GLINT_ASSET_DIR, GLINT_SHADER_DIR);
  return 0;
}
