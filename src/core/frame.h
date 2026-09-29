#pragma once

// Per-frame data both backends hand to their techniques' update(). API-agnostic by design: anything
// GL- or VK-specific (command buffer, frame-in-flight index, ...) goes in the backend's own structs.

#include <cstdint>
#include <glm/glm.hpp>

namespace glint {

class Camera;
class Input;

struct Frame {
  float dt = 0.0f;       // seconds since the previous frame
  double time = 0.0;     // seconds since start
  uint64_t index = 0;    // frame counter
  glm::ivec2 framebufferSize{0};  // pixels; what the viewport should cover
  Camera& camera;
  const Input& input;

  float aspect() const { return framebufferSize.y > 0 ? float(framebufferSize.x) / float(framebufferSize.y) : 1.0f; }
};

}  // namespace glint
