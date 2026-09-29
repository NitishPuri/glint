#include "core/camera.h"

#include <GLFW/glfw3.h>
#include <arcball_camera.h>

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

#include "core/input.h"

namespace glint {

void Camera::update(float dt, const Input& input, glm::ivec2 screenSize) {
  if (mode == Mode::Arcball) {
    if (!input.uiWantsMouse) updateArcball(dt, input, screenSize);
  } else {
    updateFly(dt, input);
  }
}

glm::mat4 Camera::view() const { return glm::lookAt(eye, target, up); }

glm::mat4 Camera::projection(float aspect, ClipDepth depth) const {
  const float fov = glm::radians(fovY);
  return depth == ClipDepth::ZeroToOne ? glm::perspectiveRH_ZO(fov, aspect, nearZ, farZ)
                                       : glm::perspectiveRH_NO(fov, aspect, nearZ, farZ);
}

void Camera::resetView() {
  const Camera defaults;
  eye = defaults.eye;
  target = defaults.target;
  up = defaults.up;
  fovY = defaults.fovY;
  nearZ = defaults.nearZ;
  farZ = defaults.farZ;
}

void Camera::updateArcball(float dt, const Input& input, glm::ivec2 screenSize) {
  // arcball_camera asserts a unit-length up vector; fly mode or UI edits may have left it slightly off.
  up = glm::normalize(up);

  // Pan and zoom scale with the distance to the target so they feel the same close up and far away.
  const float distance = glm::length(target - eye);
  const glm::vec2 p0 = input.mousePrevPos();
  const glm::vec2 p1 = input.mousePos();
  arcball_camera_update(&eye.x, &target.x, &up.x, nullptr, dt,
                        /*zoom_per_tick*/ 0.1f * distance,
                        /*pan_speed*/ 0.5f * distance,
                        /*rotation_multiplier*/ rotateSpeed, screenSize.x, screenSize.y,  //
                        int(p0.x), int(p1.x), int(p0.y), int(p1.y),
                        input.mouseDown(GLFW_MOUSE_BUTTON_MIDDLE) ? 1 : 0,
                        input.mouseDown(GLFW_MOUSE_BUTTON_RIGHT) ? 1 : 0, int(input.scrollDelta()), 0);
}

void Camera::updateFly(float dt, const Input& input) {
  // Fly mode keeps the world up so the horizon stays level (arcball may have rolled the camera).
  up = {0.0f, 1.0f, 0.0f};
  const float distance = std::max(glm::length(target - eye), 0.001f);
  glm::vec3 forward = glm::normalize(target - eye);

  if (!input.uiWantsMouse && input.mouseDown(GLFW_MOUSE_BUTTON_RIGHT)) {
    const glm::vec2 delta = input.mouseDelta() * (0.003f * rotateSpeed);  // radians per pixel
    float yaw = std::atan2(forward.z, forward.x) + delta.x;
    // Stop just short of straight up/down, where lookAt's up vector would be parallel to forward.
    float pitch = std::clamp(std::asin(std::clamp(forward.y, -1.0f, 1.0f)) - delta.y, -1.55f, 1.55f);
    forward = {std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw)};
  }

  if (!input.uiWantsKeyboard) {
    const glm::vec3 right = glm::normalize(glm::cross(forward, up));
    glm::vec3 move{0.0f};
    if (input.keyDown(GLFW_KEY_W)) move += forward;
    if (input.keyDown(GLFW_KEY_S)) move -= forward;
    if (input.keyDown(GLFW_KEY_D)) move += right;
    if (input.keyDown(GLFW_KEY_A)) move -= right;
    if (input.keyDown(GLFW_KEY_E)) move += up;
    if (input.keyDown(GLFW_KEY_Q)) move -= up;
    const float boost = input.keyDown(GLFW_KEY_LEFT_SHIFT) ? 4.0f : 1.0f;
    if (glm::dot(move, move) > 0.0f) eye += glm::normalize(move) * (moveSpeed * boost * dt);
  }

  // Keep the target in front at the same distance, so switching back to arcball orbits a sensible point.
  target = eye + forward * distance;
}

}  // namespace glint
