#pragma once

// One camera for both backends. The only API difference is the clip-space depth range, so projection()
// takes it as a parameter:
//   GL: NDC z in [-1, 1]  -> ClipDepth::NegOneToOne (glm::perspectiveRH_NO)
//   VK: NDC z in [ 0, 1]  -> ClipDepth::ZeroToOne   (glm::perspectiveRH_ZO)
// The Y difference (VK's NDC +Y points down) is *not* handled here: the VK backend uses a negative
// viewport height instead, so both APIs get the identical view/projection matrices otherwise.
// (Choosing per call avoids GLM_FORCE_DEPTH_ZERO_TO_ONE, a global define that would make glm::perspective
// mean different things in different translation units of the same program.)

#include <glm/glm.hpp>

namespace glint {

class Input;

enum class ClipDepth { NegOneToOne, ZeroToOne };

class Camera {
 public:
  enum class Mode {
    Arcball,  // right-drag rotate around target, middle-drag pan, wheel zoom
    Fly,      // right-drag look, WASD move, Q/E down/up, Shift faster
  };

  Mode mode = Mode::Arcball;
  glm::vec3 eye{4.0f, 3.0f, -3.0f};  // opengl-tutorial's default view
  glm::vec3 target{0.0f};
  glm::vec3 up{0.0f, 1.0f, 0.0f};
  float fovY = 45.0f;  // degrees
  float nearZ = 0.1f;
  float farZ = 100.0f;

  float rotateSpeed = 1.0f;  // arcball rotation multiplier; fly look sensitivity scale
  float moveSpeed = 3.0f;    // fly: units per second

  // screenSize must be in the same units as Input::mousePos() (window coordinates).
  void update(float dt, const Input& input, glm::ivec2 screenSize);

  glm::mat4 view() const;
  glm::mat4 projection(float aspect, ClipDepth depth) const;
  glm::mat4 viewProjection(float aspect, ClipDepth depth) const { return projection(aspect, depth) * view(); }

  // Back to the defaults above (keeps mode and speeds).
  void resetView();

 private:
  void updateArcball(float dt, const Input& input, glm::ivec2 screenSize);
  void updateFly(float dt, const Input& input);
};

}  // namespace glint
