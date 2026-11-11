#include "core/gpu.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>

#include "core/log.h"

namespace glint {

namespace {

constexpr uint32_t kNvidia = 0x10de, kAmd = 0x1002, kIntel = 0x8086;

std::string readLine(const std::filesystem::path& path) {
  std::ifstream file(path);
  std::string line;
  std::getline(file, line);
  return line;
}

std::string describe(const SystemGpu& gpu) {
  return std::format("{} {} ({}, {})", gpu.pciAddress, gpu.driver, gpu.discrete ? "discrete" : "integrated",
                     gpu.bootVga ? "boot VGA" : "not boot VGA");
}

}  // namespace

std::optional<GpuPreference> parseGpuPreference(std::string_view text) {
  if (text == "auto") return GpuPreference::Auto;
  if (text == "discrete") return GpuPreference::Discrete;
  if (text == "integrated") return GpuPreference::Integrated;
  return std::nullopt;
}

std::string_view toString(GpuPreference preference) {
  switch (preference) {
    case GpuPreference::Auto: return "auto";
    case GpuPreference::Discrete: return "discrete";
    case GpuPreference::Integrated: return "integrated";
  }
  std::unreachable();
}

void classifyGpus(std::vector<SystemGpu>& gpus) {
  for (SystemGpu& gpu : gpus) gpu.discrete = gpu.driver == "nvidia" || (gpus.size() > 1 && !gpu.bootVga);
}

std::vector<SystemGpu> listSystemGpus() {
  std::vector<SystemGpu> gpus;
#ifdef __linux__
  namespace fs = std::filesystem;
  std::error_code ec;
  for (const fs::directory_entry& entry : fs::directory_iterator("/sys/class/drm", ec)) {
    const std::string name = entry.path().filename().string();
    // card0, card1, ... (not card0-eDP-1 connectors, not renderD128 nodes)
    if (!name.starts_with("card") || name.contains('-')) continue;
    const fs::path device = fs::canonical(entry.path() / "device", ec);
    if (ec || !fs::exists(device / "vendor")) continue;
    SystemGpu gpu;
    gpu.pciAddress = device.filename().string();
    gpu.vendorId = uint32_t(std::stoul(readLine(device / "vendor"), nullptr, 16));
    gpu.driver = fs::canonical(device / "driver", ec).filename().string();
    gpu.bootVga = readLine(device / "boot_vga") == "1";
    // Runtime power management: "error" means the driver failed to suspend or resume the device, and it stays
    // unusable until a reboot (seen on an RTX 3060 laptop in hybrid mode: GSP firmware unload failed at suspend).
    if (readLine(device / "power/runtime_status") == "error") {
      gpu.unusable = "runtime power management is in the 'error' state (failed suspend/resume; reboot to recover)";
    } else if (gpu.driver.empty()) {
      gpu.unusable = "no kernel driver bound";
    }
    gpus.push_back(std::move(gpu));
  }
  std::ranges::sort(gpus, {}, &SystemGpu::pciAddress);
  classifyGpus(gpus);
#endif
  return gpus;
}

GpuChoice chooseGpu(GpuPreference preference, const std::vector<SystemGpu>& gpus) {
  GpuChoice choice;
  if (gpus.size() <= 1) return choice;  // nothing to choose

  const bool wantDiscrete = preference != GpuPreference::Integrated;
  auto usableOfKind = [&](bool discrete) {
    return std::ranges::find_if(gpus, [&](const SystemGpu& g) { return g.discrete == discrete && g.unusable.empty(); });
  };
  auto it = usableOfKind(wantDiscrete);
  if (it == gpus.end()) {
    const auto unusable = std::ranges::find(gpus, wantDiscrete, &SystemGpu::discrete);
    const std::string why = unusable != gpus.end()
                                ? std::format("{} is unusable: {}", describe(*unusable), unusable->unusable)
                                : std::format("there is no {} GPU", wantDiscrete ? "discrete" : "integrated");
    if (preference != GpuPreference::Auto) {
      throw std::runtime_error(std::format("--gpu {}: {}", toString(preference), why));
    }
    it = usableOfKind(!wantDiscrete);
    if (it == gpus.end()) return choice;
    choice.warnings.push_back(std::format("--gpu auto: {}; using the integrated GPU instead", why));
  }
  choice.gpu = *it;

  // Route GL. The boot VGA device is the one the display server renders on in hybrid mode: GL lands there by
  // default, so only the other GPU needs offload variables.
  if (choice.gpu->bootVga) return choice;
  if (choice.gpu->driver == "nvidia") {
    choice.env = {{"__NV_PRIME_RENDER_OFFLOAD", "1"}, {"__GLX_VENDOR_LIBRARY_NAME", "nvidia"}};
  } else {
    std::string address = choice.gpu->pciAddress;
    std::ranges::replace(address, ':', '_');
    std::ranges::replace(address, '.', '_');
    choice.env = {{"DRI_PRIME", "pci-" + address}};
  }
  return choice;
}

GpuChoice selectGpu(GpuPreference preference) {
  const std::vector<SystemGpu> gpus = listSystemGpus();
  for (const SystemGpu& gpu : gpus) {
    log::info("gpu      {}{}", describe(gpu), gpu.unusable.empty() ? "" : " - unusable: " + gpu.unusable);
  }
  GpuChoice choice = chooseGpu(preference, gpus);
  for (const std::string& warning : choice.warnings) log::warn("{}", warning);
  for (const auto& [name, value] : choice.env) {
    // Variables already set by the user (e.g. a launcher's "use discrete GPU") win.
#ifdef _WIN32
    if (!std::getenv(name.c_str())) _putenv_s(name.c_str(), value.c_str());
#else
    setenv(name.c_str(), value.c_str(), /*overwrite*/ 0);
#endif
  }
  std::string env;
  for (const auto& [name, value] : choice.env) env += std::format(" {}={}", name, value);
  if (choice.gpu) {
    log::info("gpu      --gpu {} -> {}{}", toString(preference), choice.gpu->pciAddress,
              env.empty() ? " (the default GPU: no routing needed)" : ", routed with" + env);
  }
  return choice;
}

uint32_t vendorIdFromName(std::string_view glVendor) {
  if (glVendor.contains("NVIDIA")) return kNvidia;
  if (glVendor.contains("AMD") || glVendor.contains("ATI")) return kAmd;
  if (glVendor.contains("Intel")) return kIntel;
  return 0;  // "Mesa" (llvmpipe, software), or something we don't know
}

void checkGpuVendor(const GpuChoice& choice, uint32_t vendorId, std::string_view deviceName) {
  if (!choice.gpu) return;
  if (vendorId == choice.gpu->vendorId) return;
  log::warn("gpu      wanted {} (vendor 0x{:04x}) but got '{}' (vendor 0x{:04x}): numbers from this run are from "
            "a different GPU than requested",
            choice.gpu->pciAddress, choice.gpu->vendorId, deviceName, vendorId);
}

}  // namespace glint
