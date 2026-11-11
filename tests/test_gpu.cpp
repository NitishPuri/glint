#include <doctest/doctest.h>

#include <stdexcept>

#include "core/config.h"
#include "core/gpu.h"

using namespace glint;

// Every optional below is checked by REQUIRE (or is a literal that parses) before it is read; clang-tidy can't see
// through doctest's macros.
// NOLINTBEGIN(bugprone-unchecked-optional-access)

namespace {
// The two-GPU laptop this was written on: AMD iGPU (boot VGA, drives the display) + NVIDIA dGPU.
std::vector<SystemGpu> hybridLaptop() {
  std::vector<SystemGpu> gpus = {{.pciAddress = "0000:01:00.0", .vendorId = 0x10de, .driver = "nvidia"},
                                 {.pciAddress = "0000:06:00.0", .vendorId = 0x1002, .driver = "amdgpu", .bootVga = true}};
  classifyGpus(gpus);
  return gpus;
}
}  // namespace

TEST_CASE("--gpu parsing") {
  const char* argv[] = {"x", "--gpu", "integrated"};
  CHECK((parseArgs(3, argv).gpu == GpuPreference::Integrated));  // (( )): doctest would print it with our toString()
  const char* bad[] = {"x", "--gpu", "nvidia"};
  CHECK_THROWS_AS(parseArgs(3, bad), std::invalid_argument);
  CHECK(std::string(toString(parseGpuPreference("discrete").value())) == "discrete");
}

TEST_CASE("classifyGpus") {
  const std::vector<SystemGpu> gpus = hybridLaptop();
  CHECK(gpus[0].discrete);
  CHECK_FALSE(gpus[1].discrete);
  // Two Mesa GPUs (AMD iGPU + AMD dGPU): the one the firmware didn't boot on is the discrete one.
  std::vector<SystemGpu> mesa = {{.pciAddress = "0000:03:00.0", .vendorId = 0x1002, .driver = "amdgpu"},
                                 {.pciAddress = "0000:00:02.0", .vendorId = 0x8086, .driver = "i915", .bootVga = true}};
  classifyGpus(mesa);
  CHECK(mesa[0].discrete);
  CHECK_FALSE(mesa[1].discrete);
}

TEST_CASE("chooseGpu routes GL with the right variables") {
  const std::vector<SystemGpu> gpus = hybridLaptop();

  const GpuChoice discrete = chooseGpu(GpuPreference::Auto, gpus);
  REQUIRE(discrete.gpu);
  CHECK(discrete.gpu.value().vendorId == 0x10de);
  CHECK(discrete.env == std::vector<std::pair<std::string, std::string>>{{"__NV_PRIME_RENDER_OFFLOAD", "1"},
                                                                         {"__GLX_VENDOR_LIBRARY_NAME", "nvidia"}});
  CHECK(discrete.warnings.empty());

  // The display GPU is where GL lands anyway: nothing to set.
  const GpuChoice integrated = chooseGpu(GpuPreference::Integrated, gpus);
  REQUIRE(integrated.gpu);
  CHECK(integrated.gpu.value().vendorId == 0x1002);
  CHECK(integrated.env.empty());

  // A Mesa dGPU gets DRI_PRIME naming the exact device (DRI_PRIME=1 could pick llvmpipe).
  std::vector<SystemGpu> mesa = {{.pciAddress = "0000:03:00.0", .vendorId = 0x1002, .driver = "amdgpu"},
                                 {.pciAddress = "0000:00:02.0", .vendorId = 0x8086, .driver = "i915", .bootVga = true}};
  classifyGpus(mesa);
  CHECK(chooseGpu(GpuPreference::Discrete, mesa).env ==
        std::vector<std::pair<std::string, std::string>>{{"DRI_PRIME", "pci-0000_03_00_0"}});

  // One GPU: nothing to choose, whatever the preference.
  CHECK_FALSE(chooseGpu(GpuPreference::Integrated, {gpus[0]}).gpu);
}

TEST_CASE("chooseGpu with an unusable dGPU") {
  std::vector<SystemGpu> gpus = hybridLaptop();
  gpus[0].unusable = "runtime power management is in the 'error' state";
  // auto falls back, loudly; an explicit request fails instead of measuring the wrong GPU.
  const GpuChoice fallback = chooseGpu(GpuPreference::Auto, gpus);
  REQUIRE(fallback.gpu);
  CHECK(fallback.gpu.value().vendorId == 0x1002);
  CHECK(fallback.warnings.size() == 1);
  CHECK_THROWS_AS(chooseGpu(GpuPreference::Discrete, gpus), std::runtime_error);
}

TEST_CASE("vendorIdFromName") {
  CHECK(vendorIdFromName("NVIDIA Corporation") == 0x10de);
  CHECK(vendorIdFromName("AMD") == 0x1002);
  CHECK(vendorIdFromName("Intel") == 0x8086);
  CHECK(vendorIdFromName("Mesa") == 0);  // llvmpipe
}

// NOLINTEND(bugprone-unchecked-optional-access)
