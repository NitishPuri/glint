#pragma once

// What a GL technique implements. The VK side has its own, different interface (vk::Technique with
// record(VkCommandBuffer, ...)); the two are deliberately not unified.

#include <memory>

#include "core/camera.h"
#include "core/frame.h"
#include "core/registry.h"

namespace glint::gl {

class Technique {
 public:
  virtual ~Technique() = default;  // destroys the technique's GL objects

  // Create GL objects. The context is current and the GL state is at the app's defaults.
  virtual void init() = 0;

  // Where the camera starts (and "Reset view" returns to) for this technique. Default: Camera's default.
  virtual void setupCamera(Camera& camera) { camera.setHome({4.0f, 3.0f, -3.0f}); }

  // Animation, input, shader hot reload, ImGui overlay drawing. No GL draw calls here.
  // (Keep ImGui calls here or in ui(), never in render(): the VK app records after ImGui::Render().)
  virtual void update(float dt, Frame& frame) {
    (void)dt;
    (void)frame;
  }

  // Draw into the default framebuffer (0), setting every piece of GL state the draws depend on:
  // GL state is global and whatever the previous frame (or ImGui) left behind is still there.
  // (VK: that state lives in the pipeline object instead, so it can't leak.)
  virtual void render(const Frame& frame) = 0;

  // Widgets for this technique, drawn inside the app's main panel (usually the shared params panel).
  virtual void ui() {}
};

using Registry = glint::Registry<Technique>;

}  // namespace glint::gl

// Registers a technique under `name`. The .cpp must be compiled into glint_gl directly (see core/registry.h).
#define GLINT_REGISTER_GL(name, Type)                                      \
  static const glint::Registrar<glint::gl::Technique> s_glintRegistrar_gl{ \
      name, [] { return std::make_unique<Type>(); }}
