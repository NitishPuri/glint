// glint_vk — the Vulkan app: window, context, swapchain, frames in flight, ImGui, technique registry, loop.
//
// Compare with app_gl/main.cpp. Everything GL's driver does behind glfwSwapBuffers is spelled out here:
// acquiring an image, waiting for a free frame slot, recording a command buffer, layout transitions,
// submitting with semaphores, presenting, and rebuilding the swapchain when the window changes size.

#include "vk/vk.h"  // before GLFW, so it declares the Vulkan parts
//
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <memory>
#include <optional>
#include <stdexcept>

#include "core/assets.h"
#include "core/camera.h"
#include "core/config.h"
#include "core/log.h"
#include "core/renderdoc.h"
#include "core/run_log.h"
#include "core/timer.h"
#include "core/ui.h"
#include "core/window.h"
#include "vk/barrier.h"
#include "vk/context.h"
#include "vk/debug.h"
#include "vk/frame.h"
#include "vk/gpu_queries.h"
#include "vk/swapchain.h"
#include "vk/technique.h"

using namespace glint;

namespace {

std::unique_ptr<vk::Technique> createTechnique(const std::string& name, vk::Context& ctx, VkFormat format,
                                               Camera& camera) {
  std::unique_ptr<vk::Technique> technique = vk::Registry::instance().create(name);
  if (!technique) throw std::runtime_error(fmt::format("unknown technique '{}'", name));
  try {
    technique->init(ctx, format);
  } catch (const std::exception& e) {
    log::error("{}: init failed: {}", name, e.what());
    return nullptr;
  }
  technique->setupCamera(camera);
  log::info("technique {}", name);
  return technique;
}

// --screenshot: a host-visible buffer the swapchain image gets copied into (before ImGui is drawn).
struct Readback {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkExtent2D extent{};
  VkFormat format = VK_FORMAT_UNDEFINED;
};

Readback createReadback(const vk::Context& ctx, VkExtent2D extent, VkFormat format) {
  Readback r{VK_NULL_HANDLE, VK_NULL_HANDLE, extent, format};
  VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  info.size = VkDeviceSize(extent.width) * extent.height * 4;
  info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  VK_CHECK(vkCreateBuffer(ctx.device, &info, nullptr, &r.buffer));
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(ctx.device, r.buffer, &req);
  VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  alloc.allocationSize = req.size;
  alloc.memoryTypeIndex = ctx.findMemoryType(
      req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VK_CHECK(vkAllocateMemory(ctx.device, &alloc, nullptr, &r.memory));
  VK_CHECK(vkBindBufferMemory(ctx.device, r.buffer, r.memory, 0));
  return r;
}

// Records: image COLOR_ATTACHMENT -> TRANSFER_SRC, copy to the buffer, image back to COLOR_ATTACHMENT.
void recordReadback(VkCommandBuffer cmd, VkImage image, const Readback& r) {
  vk::transitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                      VK_ACCESS_2_TRANSFER_READ_BIT);
  VkBufferImageCopy region{};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {r.extent.width, r.extent.height, 1};
  vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, r.buffer, 1, &region);
  vk::transitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                      VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
  // Make the copy's writes visible to the CPU once the fence has signalled.
  VkBufferMemoryBarrier2 toHost{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
  toHost.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
  toHost.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
  toHost.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
  toHost.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
  toHost.srcQueueFamilyIndex = toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toHost.buffer = r.buffer;
  toHost.size = VK_WHOLE_SIZE;
  VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dependency.bufferMemoryBarrierCount = 1;
  dependency.pBufferMemoryBarriers = &toHost;
  vkCmdPipelineBarrier2(cmd, &dependency);
}

void saveReadback(const vk::Context& ctx, const Readback& r, const std::string& path) {
  ImageData image;
  image.width = int(r.extent.width);
  image.height = int(r.extent.height);
  image.channels = 4;
  image.pixels.resize(size_t(image.width) * image.height * 4);
  void* mapped = nullptr;
  VK_CHECK(vkMapMemory(ctx.device, r.memory, 0, VK_WHOLE_SIZE, 0, &mapped));
  std::memcpy(image.pixels.data(), mapped, image.pixels.size());
  vkUnmapMemory(ctx.device, r.memory);
  // Swapchain is BGRA; PNG wants RGBA. Row 0 is already the top row (VK images start at the top-left,
  // unlike GL's bottom-left), so no flip.
  if (r.format == VK_FORMAT_B8G8R8A8_UNORM || r.format == VK_FORMAT_B8G8R8A8_SRGB) {
    for (size_t i = 0; i < image.pixels.size(); i += 4) std::swap(image.pixels[i], image.pixels[i + 2]);
  }
  for (size_t i = 3; i < image.pixels.size(); i += 4) image.pixels[i] = 255;
  writePng(path, image);
  log::info("screenshot saved to {}", path);
}

void runApp(const Config& config) {
  Window window({.title = "glint - Vulkan",
                 .width = config.width,
                 .height = config.height,
                 .api = GraphicsApi::Vulkan});

  // GL: context + function pointers in two lines. VK: instance, validation, surface, GPU choice, device, queue.
  vk::Context ctx({.window = window.handle(), .validation = config.validation});
  const glm::ivec2 fb = window.framebufferSize();
  vk::Swapchain swapchain(ctx, {uint32_t(fb.x), uint32_t(fb.y)}, config.vsync);
  vk::Frames frames(ctx);
  vk::GpuQueries gpuQueries(ctx);  // GPU time + pipeline statistics of the technique's commands (not ImGui)

  // --- ImGui: GLFW platform backend + Vulkan renderer backend, drawing with dynamic rendering -----------
  ui::init();
  ImGui_ImplGlfw_InitForVulkan(window.handle(), /*install_callbacks*/ true);
  ImGui_ImplVulkan_InitInfo imguiInfo{};
  imguiInfo.ApiVersion = VK_API_VERSION_1_3;
  imguiInfo.Instance = ctx.instance;
  imguiInfo.PhysicalDevice = ctx.physicalDevice;
  imguiInfo.Device = ctx.device;
  imguiInfo.QueueFamily = ctx.queueFamily;
  imguiInfo.Queue = ctx.queue;
  imguiInfo.DescriptorPoolSize = 64;  // let the backend create its own descriptor pool (fonts + UI images)
  imguiInfo.MinImageCount = swapchain.minImageCount;
  imguiInfo.ImageCount = uint32_t(swapchain.images.size());
  imguiInfo.UseDynamicRendering = true;
  imguiInfo.PipelineInfoMain.PipelineRenderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  imguiInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
  imguiInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchain.format;
  ImGui_ImplVulkan_Init(&imguiInfo);

  const std::vector<std::string> names = vk::Registry::instance().names();
  if (names.empty()) throw std::runtime_error("no VK techniques registered");
  std::string current = config.technique.empty() ? names.front() : config.technique;
  Camera camera;
  // Baseline for the per-technique summary; taken before init() so init-time issues count too.
  int issuesAtStart = vk::Context::validationIssueCount();
  std::unique_ptr<vk::Technique> technique = createTechnique(current, ctx, swapchain.format, camera);

  Timer frameTimer;
  const Timer startTimer;
  ui::FrameStats stats;
  FrameTimeSummary summary, cpuSummary, gpuSummary;
  auto logSummary = [&] {
    if (summary.count() == 0) return;
    log::info("summary  {}: {}, validation issues {}; cpu {}; gpu {}", current, summary.describe(),
              vk::Context::validationIssueCount() - issuesAtStart, cpuSummary.meanP95(), gpuSummary.meanP95());
    summary.clear();
    cpuSummary.clear();
    gpuSummary.clear();
    issuesAtStart = vk::Context::validationIssueCount();
  };
  Timer cpuTimer;
  float lastCpuMs = 0.0f;
  const std::string device = ctx.deviceDescription();
  std::optional<Readback> readback;

  auto recreateSwapchain = [&] {
    window.waitWhileMinimized();
    // The old swapchain's images may still be in use by queued frames: wait for everything first.
    // (Simple and correct; fancier: keep the old swapchain alive until its frames retire.)
    VK_CHECK(vkDeviceWaitIdle(ctx.device));
    const glm::ivec2 size = window.framebufferSize();
    swapchain.recreate({uint32_t(size.x), uint32_t(size.y)});
    ImGui_ImplVulkan_SetMinImageCount(swapchain.minImageCount);
    frameTimer.reset();
  };

  uint32_t frameInFlight = 0;
  for (uint64_t frameIndex = 0; !window.shouldClose(); ++frameIndex) {
    window.pollEvents();
    if (window.isMinimized()) {
      window.waitWhileMinimized();
      frameTimer.reset();
      continue;
    }
    vk::FrameSync& sync = frames[frameInFlight];

    // --- 1. wait until this frame slot's previous submission is done --------------------------------------
    // Then its command buffer (and any per-frame buffers) can be reused. GL: the driver does this.
    VK_CHECK(vkWaitForFences(ctx.device, 1, &sync.inFlight, VK_TRUE, UINT64_MAX));
    // The slot is free, so its queries from kFramesInFlight frames ago are done: read them (no stall).
    gpuQueries.collect(frameInFlight);

    // --- 2. get a swapchain image to render into -----------------------------------------------------------
    uint32_t imageIndex = 0;
    const VkResult acquired = vkAcquireNextImageKHR(ctx.device, swapchain.handle, UINT64_MAX, sync.imageAcquired,
                                                    VK_NULL_HANDLE, &imageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {  // window resized: this swapchain is unusable
      recreateSwapchain();
      continue;
    }
    if (acquired != VK_SUBOPTIMAL_KHR) VK_CHECK(acquired);  // suboptimal still works; recreated after present
    // Reset only now that we'll definitely submit: resetting and then bailing out would leave the fence
    // unsignalled forever and deadlock the next wait.
    VK_CHECK(vkResetFences(ctx.device, 1, &sync.inFlight));
    // CPU time starts after the waits (fence + acquire) and ends at submit: the app's own work.
    cpuTimer.reset();

    const float dt = float(frameTimer.lap());
    // Simulation time: real, or fixed steps (--fixed-dt) so scripted runs (parity tool) render the same frame
    // in both apps regardless of how fast each one runs.
    const float simDt = config.fixedDt > 0.0f ? config.fixedDt : dt;
    const double simTime = config.fixedDt > 0.0f ? double(frameIndex) * config.fixedDt : startTimer.elapsed();
    const GpuStats& gpu = gpuQueries.stats();
    stats.add(dt, lastCpuMs, gpu.valid ? float(gpu.gpuMs) : -1.0f);
    summary.add(dt);
    cpuSummary.add(lastCpuMs * 1e-3f);
    if (gpu.valid) gpuSummary.add(float(gpu.gpuMs) * 1e-3f);
    const glm::ivec2 fbSize = {int(swapchain.extent.width), int(swapchain.extent.height)};

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ui::newFrame(window.input());
    // Scripted screenshot runs ignore input: the window opens under the mouse, and a stray scroll would
    // move the camera (one 11_gltf GL/VK comparison differed for exactly that reason).
    if (config.screenshot.empty()) camera.update(simDt, window.input(), window.framebufferSize());
    Frame frame{simDt, simTime, frameIndex, fbSize, camera, window.input()};
    if (technique) technique->update(simDt, frame);

    // UI for this frame (recorded into the command buffer below).
    std::optional<std::string> picked;
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("glint - Vulkan");
    picked = ui::techniqueCombo(names, current);
    ui::statsSection(stats, gpuQueries.stats(), "Vulkan 1.3", device, fbSize.x, fbSize.y);
    if (config.validation) ImGui::Text("validation issues: %d", vk::Context::validationIssueCount());
    ui::cameraSection(camera);
    renderdoc::uiSection();
    if (technique && ImGui::CollapsingHeader(current.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) technique->ui();
    ImGui::End();
    ImGui::Render();

    const bool lastFrame = config.exitAfterFrames > 0 && frameIndex + 1 >= uint64_t(config.exitAfterFrames);
    const bool screenshot = lastFrame && !config.screenshot.empty();
    if (screenshot) readback = createReadback(ctx, swapchain.extent, swapchain.format);

    // --- 3. record the frame's commands ---------------------------------------------------------------------
    VkCommandBuffer cmd = sync.commandBuffer;
    VK_CHECK(vkResetCommandPool(ctx.device, sync.commandPool, 0));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &begin));

    VkImage image = swapchain.images[imageIndex];
    // Freshly acquired image -> color attachment. UNDEFINED: we'll overwrite it, the old contents don't matter.
    // The src stage matches the stage the submit waits for imageAcquired at, which chains the two together.
    vk::transitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    if (technique) {
      vk::debug::Label label(cmd, current);  // groups the technique's commands in RenderDoc
      gpuQueries.begin(cmd, frameInFlight);
      technique->record(cmd, {frame, frameInFlight, image, swapchain.views[imageIndex], swapchain.format,
                              swapchain.extent});
      gpuQueries.end(cmd, frameInFlight);
    } else {
      // Technique failed to initialise: clear to dark red, like the GL app.
      VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
      color.imageView = swapchain.views[imageIndex];
      color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      color.clearValue.color = {{0.3f, 0.05f, 0.05f, 1.0f}};
      VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
      rendering.renderArea = {{0, 0}, swapchain.extent};
      rendering.layerCount = 1;
      rendering.colorAttachmentCount = 1;
      rendering.pColorAttachments = &color;
      vkCmdBeginRendering(cmd, &rendering);
      vkCmdEndRendering(cmd);
    }

    if (screenshot) recordReadback(cmd, image, *readback);  // before the UI, like the GL app

    {
      // ImGui on top: LOAD keeps what the technique drew.
      vk::debug::Label label(cmd, "imgui");
      VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
      color.imageView = swapchain.views[imageIndex];
      color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
      color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
      rendering.renderArea = {{0, 0}, swapchain.extent};
      rendering.layerCount = 1;
      rendering.colorAttachmentCount = 1;
      rendering.pColorAttachments = &color;
      vkCmdBeginRendering(cmd, &rendering);
      ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
      vkCmdEndRendering(cmd);
    }

    // Color attachment -> presentable. Nothing after this in our queue touches the image, so dst is NONE;
    // the renderDone semaphore makes presentation wait.
    vk::transitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, 0);
    VK_CHECK(vkEndCommandBuffer(cmd));

    // --- 4. submit: wait for the image, signal "render done" for present, signal the fence for the CPU -----
    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = sync.imageAcquired;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;  // only color output has to wait for it
    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = swapchain.renderDone[imageIndex];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = cmd;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;
    VK_CHECK(vkQueueSubmit2(ctx.queue, 1, &submit, sync.inFlight));
    lastCpuMs = float(cpuTimer.elapsed() * 1000.0);

    // --- 5. present --------------------------------------------------------------------------------------
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &swapchain.renderDone[imageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain.handle;
    present.pImageIndices = &imageIndex;
    const VkResult presented = vkQueuePresentKHR(ctx.queue, &present);
    // GLFW's resize event can arrive a frame *after* present already reported OUT_OF_DATE and we rebuilt;
    // only rebuild again if the size really differs from the swapchain's.
    const glm::ivec2 windowFb = window.framebufferSize();
    const bool resized = window.consumeResized() && (uint32_t(windowFb.x) != swapchain.extent.width ||
                                                     uint32_t(windowFb.y) != swapchain.extent.height);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR || resized) {
      recreateSwapchain();
    } else {
      VK_CHECK(presented);
    }

    if (screenshot) {
      VK_CHECK(vkWaitForFences(ctx.device, 1, &sync.inFlight, VK_TRUE, UINT64_MAX));
      saveReadback(ctx, *readback, config.screenshot);
    }

    if (picked) {
      logSummary();
      // GL deletes objects whenever; VK objects must not be destroyed while queued frames still use them.
      VK_CHECK(vkDeviceWaitIdle(ctx.device));
      technique.reset();
      current = *picked;
      technique = createTechnique(current, ctx, swapchain.format, camera);
      // Don't count the new technique's load time (seconds for 11_gltf) as a frame in its summary.
      frameTimer.reset();
    }
    if (lastFrame) window.requestClose();
    frameInFlight = (frameInFlight + 1) % vk::kFramesInFlight;
  }

  // --- shutdown: GPU idle first, then destroy in reverse creation order ------------------------------------
  VK_CHECK(vkDeviceWaitIdle(ctx.device));
  logSummary();
  technique.reset();
  if (readback) {
    vkDestroyBuffer(ctx.device, readback->buffer, nullptr);
    vkFreeMemory(ctx.device, readback->memory, nullptr);
  }
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ui::shutdown();
  // frames, swapchain and ctx are destroyed by their destructors, in that order (reverse of declaration).
  // Validation reports any object we forgot to destroy when the device goes away.
}

// RenderDoc captures VK through an implicit *layer*, which the loader only finds via a registered manifest.
// The tarball's manifest has a library_path from RenderDoc's build machine (/io/dist/...), so write a fixed
// copy into the build dir and point the loader at it for this process only (qrenderdoc's "register layer"
// writes the same thing to ~/.local/share/vulkan/implicit_layer.d for everyone).
void enableRenderDocLayer() {
  const std::filesystem::path installed =
      std::filesystem::path(renderdoc::installDir()) / "etc/vulkan/implicit_layer.d/renderdoc_capture.json";
  std::ifstream in(installed);
  if (!in) {
    log::warn("RenderDoc: no layer manifest at {}; VK frames can't be captured", installed.string());
    return;
  }
  std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const std::string library = renderdoc::installDir() + "/lib/librenderdoc.so";
  json = std::regex_replace(json, std::regex(R"("library_path"\s*:\s*"[^"]*")"),
                            "\"library_path\": \"" + library + "\"");
  const std::filesystem::path dir = std::filesystem::path(GLINT_BUILD_DIR) / "renderdoc_layer";
  std::filesystem::create_directories(dir);
  std::ofstream(dir / "renderdoc_capture.json") << json;
  // Read by the loader at vkCreateInstance: an extra implicit-layer directory, and the layer's opt-in switch.
  setenv("VK_ADD_IMPLICIT_LAYER_PATH", dir.string().c_str(), 1);
  setenv("ENABLE_VULKAN_RENDERDOC_CAPTURE", "1", 1);
}

int run(const Config& config) {
  if (config.renderdoc) {
#ifndef _WIN32  // on Windows the RenderDoc installer registers the layer
    enableRenderDocLayer();
#endif
    renderdoc::load("glint_vk");  // before the instance exists
  }
  runApp(config);  // everything VK is destroyed when this returns, so leak reports are counted below
  if (config.validation) log::info("validation issues (warnings + errors): {}", vk::Context::validationIssueCount());
  return config.validation && vk::Context::validationIssueCount() > 0 ? 2 : 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Config config = parseArgs(argc, argv);
    if (config.showHelp) {
      fmt::print("{}", usage(argv[0]));
      fmt::print("techniques:\n");
      for (const std::string& name : vk::Registry::instance().names()) fmt::print("  {}\n", name);
      return 0;
    }
    // Check the name before creating any GPU objects: failing halfway through startup would skip the
    // orderly shutdown (and, in VK, show up as a pile of "object not destroyed" validation errors).
    if (!config.technique.empty() && !vk::Registry::instance().contains(config.technique)) {
      log::error("unknown technique '{}' (see --help for the list)", config.technique);
      return 1;
    }
    startRunLog("glint_vk", config, argc, argv);
    if (!config.assetDir.empty()) setAssetDir(config.assetDir);
    return run(config);
  } catch (const std::exception& e) {
    log::error("{}", e.what());
    return 1;
  }
}
