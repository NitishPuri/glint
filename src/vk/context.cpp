#include "vk/context.h"

// GLFW only declares glfwCreateWindowSurface if <vulkan/vulkan.h> came first (vk/context.h includes it).
#include <GLFW/glfw3.h>

#include <atomic>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "core/log.h"
#include "vk/debug.h"

namespace glint::vk {

namespace {

std::atomic<int> g_validationIssues{0};

// The validation layer (and the loader) report through this. GL equivalent: the KHR_debug callback.
VKAPI_ATTR VkBool32 VKAPI_CALL onDebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT types,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data, void* /*user*/) {
  // Only messages about *our* API usage count as issues. GENERAL ones come from the loader and describe
  // the environment (e.g. "duplicate layer" when both the SDK's and the distro's validation layer exist).
  const bool aboutUs = types & (VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT);
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    if (aboutUs) ++g_validationIssues;
    log::error("VK {}", data->pMessage);
  } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    if (aboutUs) ++g_validationIssues;
    log::warn("VK {}", data->pMessage);
  } else {
    log::debug("VK {}", data->pMessage);
  }
  return VK_FALSE;  // don't abort the call that triggered it
}

VkDebugUtilsMessengerCreateInfoEXT messengerInfo() {
  VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
  info.messageSeverity =
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  info.pfnUserCallback = onDebugMessage;
  return info;
}

bool hasLayer(const char* name) {
  uint32_t count = 0;
  vkEnumerateInstanceLayerProperties(&count, nullptr);
  std::vector<VkLayerProperties> layers(count);
  vkEnumerateInstanceLayerProperties(&count, layers.data());
  for (const VkLayerProperties& layer : layers) {
    if (std::strcmp(layer.layerName, name) == 0) return true;
  }
  return false;
}

bool hasDeviceExtension(VkPhysicalDevice gpu, const char* name) {
  uint32_t count = 0;
  vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> extensions(count);
  vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, extensions.data());
  for (const VkExtensionProperties& ext : extensions) {
    if (std::strcmp(ext.extensionName, name) == 0) return true;
  }
  return false;
}

// A queue family that can do graphics and present to our surface, or UINT32_MAX.
uint32_t findQueueFamily(VkPhysicalDevice gpu, VkSurfaceKHR surface) {
  uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, families.data());
  for (uint32_t i = 0; i < count; ++i) {
    VkBool32 present = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &present);
    if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) return i;
  }
  return UINT32_MAX;
}

// Higher is better; < 0 = unusable.
int scoreDevice(VkPhysicalDevice gpu, VkSurfaceKHR surface) {
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(gpu, &props);
  if (props.apiVersion < VK_API_VERSION_1_3) return -1;
  if (!hasDeviceExtension(gpu, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) return -1;
  if (findQueueFamily(gpu, surface) == UINT32_MAX) return -1;
  switch (props.deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 3;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 2;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 1;
    default: return -1;  // CPU (llvmpipe/lavapipe): works, but it's not what we want to measure
  }
}

}  // namespace

Context::Context(const ContextDesc& desc) {
  // --- instance ------------------------------------------------------------------------------------------
  // The connection to the Vulkan loader. apiVersion = the highest version *we* use; the driver may be newer.
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "glint";
  app.pEngineName = "glint";
  app.apiVersion = VK_API_VERSION_1_3;

  // Instance extensions: whatever GLFW needs for window surfaces (VK_KHR_surface + VK_KHR_xlib_surface on X11),
  // plus debug utils for validation messages and object names.
  uint32_t glfwCount = 0;
  const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwCount);
  if (!glfwExtensions) throw std::runtime_error("GLFW: Vulkan not supported (no loader or no surface extension)");
  std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwCount);
  extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

  // Validation is a *layer*: separate code inserted between us and the driver that checks every call.
  // GL's KHR_debug checks are built into the driver; VK drivers assume correct usage and check nothing.
  std::vector<const char*> layers;
  const bool validation = desc.validation && hasLayer("VK_LAYER_KHRONOS_validation");
  if (desc.validation && !validation) {
    log::warn("VK_LAYER_KHRONOS_validation not found (source the Vulkan SDK's setup-env.sh); running without it");
  }
  if (validation) layers.push_back("VK_LAYER_KHRONOS_validation");

  // Also check synchronisation (missing barriers, write-after-read races, ...): off by default in the layer,
  // and exactly the class of bug GL never lets you make.
  const VkValidationFeatureEnableEXT enabledFeatures[] = {VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT};
  VkValidationFeaturesEXT validationFeatures{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
  validationFeatures.enabledValidationFeatureCount = 1;
  validationFeatures.pEnabledValidationFeatures = enabledFeatures;
  // Chained into instance creation, the messenger also reports problems in vkCreateInstance/vkDestroyInstance.
  VkDebugUtilsMessengerCreateInfoEXT messengerCreate = messengerInfo();
  messengerCreate.pNext = &validationFeatures;

  VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  instanceInfo.pNext = validation ? &messengerCreate : nullptr;
  instanceInfo.pApplicationInfo = &app;
  instanceInfo.enabledExtensionCount = uint32_t(extensions.size());
  instanceInfo.ppEnabledExtensionNames = extensions.data();
  instanceInfo.enabledLayerCount = uint32_t(layers.size());
  instanceInfo.ppEnabledLayerNames = layers.data();
  VK_CHECK(vkCreateInstance(&instanceInfo, nullptr, &instance));
  debug::load(instance);

  if (validation) {
    const VkDebugUtilsMessengerCreateInfoEXT info = messengerInfo();
    auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    VK_CHECK(create(instance, &info, nullptr, &messenger));
    log::info("validation: VK_LAYER_KHRONOS_validation + synchronization validation");
  }

  // --- surface -------------------------------------------------------------------------------------------
  // The window as a presentation target. (GL: the window already *is* the default framebuffer.)
  VK_CHECK(glfwCreateWindowSurface(instance, desc.window, nullptr, &surface));

  // --- physical device -----------------------------------------------------------------------------------
  // GL runs on whatever GPU the window system picked. VK lists them all (here: RADV, llvmpipe, and NVIDIA
  // under PRIME offload) and lets you choose.
  uint32_t gpuCount = 0;
  vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr);
  std::vector<VkPhysicalDevice> gpus(gpuCount);
  vkEnumeratePhysicalDevices(instance, &gpuCount, gpus.data());
  int bestScore = -1;
  for (VkPhysicalDevice gpu : gpus) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(gpu, &props);
    const int score = scoreDevice(gpu, surface);
    log::info("GPU        {} ({}){}", props.deviceName, string_VkPhysicalDeviceType(props.deviceType),
              score < 0 ? " - skipped" : "");
    if (score > bestScore) {
      bestScore = score;
      physicalDevice = gpu;
    }
  }
  if (!physicalDevice) throw std::runtime_error("no Vulkan 1.3 GPU with graphics + present + swapchain support");
  vkGetPhysicalDeviceProperties(physicalDevice, &properties);
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
  queueFamily = findQueueFamily(physicalDevice, surface);

  // --- logical device + queue ----------------------------------------------------------------------------
  // Features are opt-in: anything not enabled here is invalid to use, even if the GPU supports it.
  // The two 1.3 features this project is built on: dynamic rendering (no VkRenderPass/VkFramebuffer) and
  // synchronization2 (vkCmdPipelineBarrier2, vkQueueSubmit2).
  VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  features13.dynamicRendering = VK_TRUE;
  features13.synchronization2 = VK_TRUE;
  VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  features.pNext = &features13;
  features.features.samplerAnisotropy = VK_TRUE;  // used from 03 on; every desktop GPU has it

  const float priority = 1.0f;
  VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  queueInfo.queueFamilyIndex = queueFamily;
  queueInfo.queueCount = 1;
  queueInfo.pQueuePriorities = &priority;

  const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  deviceInfo.pNext = &features;  // features go in pNext (pEnabledFeatures must then be null)
  deviceInfo.queueCreateInfoCount = 1;
  deviceInfo.pQueueCreateInfos = &queueInfo;
  deviceInfo.enabledExtensionCount = 1;
  deviceInfo.ppEnabledExtensionNames = deviceExtensions;
  VK_CHECK(vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device));
  vkGetDeviceQueue(device, queueFamily, 0, &queue);

  debug::setName(device, VK_OBJECT_TYPE_QUEUE, queue, "main queue");
  log::info("device     {}", deviceDescription());
}

Context::~Context() {
  // Children before parents: device before instance, surface and messenger before instance.
  vkDestroyDevice(device, nullptr);
  vkDestroySurfaceKHR(instance, surface, nullptr);
  if (messenger) {
    auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    destroy(instance, messenger, nullptr);
  }
  vkDestroyInstance(instance, nullptr);
}

uint32_t Context::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags flags) const {
  for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
    if ((typeBits & (1u << i)) && (memoryProperties.memoryTypes[i].propertyFlags & flags) == flags) return i;
  }
  throw std::runtime_error(fmt::format("no memory type with flags {}", string_VkMemoryPropertyFlags(flags)));
}

std::string Context::deviceDescription() const {
  VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
  VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
  props.pNext = &driver;
  vkGetPhysicalDeviceProperties2(physicalDevice, &props);
  return fmt::format("{}, {} {}, Vulkan {}.{}.{}", properties.deviceName, driver.driverName, driver.driverInfo,
                     VK_API_VERSION_MAJOR(properties.apiVersion), VK_API_VERSION_MINOR(properties.apiVersion),
                     VK_API_VERSION_PATCH(properties.apiVersion));
}

int Context::validationIssueCount() { return g_validationIssues.load(); }

}  // namespace glint::vk
