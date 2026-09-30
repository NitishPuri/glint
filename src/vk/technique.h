#pragma once

// What a VK technique implements. Compare with gl/technique.h: same lifecycle, but instead of drawing
// right away, record() *records commands* into a command buffer the app submits later.

#include <memory>

#include "core/camera.h"
#include "core/frame.h"
#include "core/registry.h"
#include "vk/context.h"

namespace glint::vk {

// Per-frame Vulkan specifics handed to record(), next to the API-agnostic Frame.
struct FrameInfo {
  const Frame& frame;
  uint32_t frameInFlight;  // 0..kFramesInFlight-1: index for per-frame resources (uniform buffers, ...)
  // The swapchain image to render into. On entry it is in COLOR_ATTACHMENT_OPTIMAL layout (the app did the
  // transition) and record() must leave it there: the app draws ImGui on top, then presents it.
  VkImage target;
  VkImageView targetView;
  VkFormat targetFormat;
  VkExtent2D extent;
};

class Technique {
 public:
  // Destroy the technique's VK objects. The app has waited for the GPU to be idle before this runs.
  virtual ~Technique() = default;

  // Create VK objects (buffers, pipelines, ...). swapchainFormat: the color format pipelines render to.
  virtual void init(Context& ctx, VkFormat swapchainFormat) = 0;

  // Where the camera starts (and "Reset view" returns to). Same as the GL side.
  virtual void setupCamera(Camera& camera) { camera.setHome({4.0f, 3.0f, -3.0f}); }

  // Animation, input, per-frame buffer updates. No command recording here.
  virtual void update(float dt, Frame& frame) {
    (void)dt;
    (void)frame;
  }

  // Record this frame's commands: vkCmdBeginRendering on info.targetView ... vkCmdEndRendering.
  virtual void record(VkCommandBuffer cmd, const FrameInfo& info) = 0;

  virtual void ui() {}
};

using Registry = glint::Registry<Technique>;

}  // namespace glint::vk

// Registers a technique under `name`. The .cpp must be compiled into glint_vk directly (see core/registry.h).
#define GLINT_REGISTER_VK(name, Type)                                      \
  static const glint::Registrar<glint::vk::Technique> s_glintRegistrar_vk{ \
      name, [] { return std::make_unique<Type>(); }}
