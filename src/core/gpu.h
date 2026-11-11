#pragma once

// Which GPU to run on, on machines with more than one (hybrid laptops, desktops with an iGPU + a dGPU).
//
// VK chooses a VkPhysicalDevice itself, but GL takes whatever GPU the window system hands it, which on a hybrid
// laptop is the one driving the display (usually the iGPU). Without help, glint_vk would run on the dGPU and
// glint_gl on the iGPU, and every GL-vs-VK number would compare two different GPUs. So both apps take the same
// --gpu preference (default: $GLINT_GPU, else auto), pick a GPU from the OS's list *before* the window or the VK
// instance exist, and set the environment variables that route GL there (Linux PRIME render offload):
//   NVIDIA proprietary driver: __NV_PRIME_RENDER_OFFLOAD=1 + __GLX_VENDOR_LIBRARY_NAME=nvidia (libglvnd picks
//                              NVIDIA's GLX; the same variable enables NVIDIA's VK_LAYER_NV_optimus)
//   Mesa drivers:              DRI_PRIME=pci-0000_01_00_0 (that exact device; plain DRI_PRIME=1 means "not the
//                              default one", which on an NVIDIA-proprietary system lands on llvmpipe)
// Then each app checks the vendor it actually got. No GL or VK types here: this is OS-level.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace glint {

enum class GpuPreference {
  Auto,        // discrete if one is usable, else integrated (with a warning)
  Discrete,    // must be discrete: an error if there's none, or it's unusable
  Integrated,  // must be integrated
};

std::optional<GpuPreference> parseGpuPreference(std::string_view text);  // "auto" | "discrete" | "integrated"
std::string_view toString(GpuPreference preference);

// A GPU as the OS lists it (Linux: /sys/class/drm/card*/device).
struct SystemGpu {
  std::string pciAddress;  // "0000:01:00.0"
  uint32_t vendorId = 0;   // PCI vendor = VkPhysicalDeviceProperties::vendorID (0x10de NVIDIA, 0x1002 AMD, 0x8086 Intel)
  std::string driver;      // kernel driver: nvidia, amdgpu, i915, xe, nouveau, ...
  bool bootVga = false;    // the GPU the firmware booted on
  bool discrete = false;   // see classifyGpus()
  std::string unusable;    // non-empty: why it can't be used right now
};

// Linux: every DRM card with a PCI device, classified. Elsewhere: empty (the platform decides; on Windows, the
// driver's per-app setting).
std::vector<SystemGpu> listSystemGpus();

// The sysfs has no "discrete" flag, so: NVIDIA's proprietary driver only drives discrete GPUs; otherwise, with
// several GPUs, the boot VGA device is the integrated one (the firmware boots on the iGPU on hybrid laptops; a
// desktop booting on its dGPU usually has the iGPU disabled). With a single GPU the question doesn't arise.
void classifyGpus(std::vector<SystemGpu>& gpus);

struct GpuChoice {
  std::optional<SystemGpu> gpu;  // empty: let the platform decide (one GPU or none listed)
  std::vector<std::pair<std::string, std::string>> env;  // variables to set before the window / instance exist
  std::vector<std::string> warnings;  // e.g. auto fell back to the integrated GPU
};

// Pure: picks a GPU from `gpus` and the variables that route GL to it. Throws std::runtime_error when an explicit
// preference can't be met.
GpuChoice chooseGpu(GpuPreference preference, const std::vector<SystemGpu>& gpus);

// listSystemGpus + chooseGpu + setenv, with log lines. Call first thing in run(), before the window.
GpuChoice selectGpu(GpuPreference preference);

// GL reports its vendor as a string (GL_VENDOR): "NVIDIA Corporation", "AMD", "Intel", "Mesa" (llvmpipe).
// 0 = not a hardware vendor we know.
uint32_t vendorIdFromName(std::string_view glVendor);

// After the context / device exists: log whether the app got the chosen GPU's vendor; warn if not.
void checkGpuVendor(const GpuChoice& choice, uint32_t vendorId, std::string_view deviceName);

}  // namespace glint
