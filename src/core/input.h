#pragma once

// Keyboard/mouse state for one window, fed by the Window's GLFW callbacks.
// Key and button codes are GLFW's (GLFW_KEY_W, GLFW_MOUSE_BUTTON_RIGHT, ...).

#include <array>
#include <glm/glm.hpp>

namespace glint {

class Input {
 public:
  bool keyDown(int key) const;
  bool keyPressed(int key) const;  // went down since the last beginFrame()
  bool mouseDown(int button) const;
  glm::vec2 mousePos() const { return m_mousePos; }        // window coordinates, origin top-left
  glm::vec2 mousePrevPos() const { return m_mousePrevPos; }  // at the last beginFrame()
  glm::vec2 mouseDelta() const { return m_mousePos - m_mousePrevPos; }
  float scrollDelta() const { return m_scroll; }  // wheel ticks since the last beginFrame()

  // Set each frame by the UI layer from ImGui's WantCapture* so the camera ignores input aimed at a panel.
  bool uiWantsMouse = false;
  bool uiWantsKeyboard = false;

  // Clears per-frame deltas. Window::pollEvents() calls it right before glfwPollEvents().
  void beginFrame();

  // Called from the Window's GLFW callbacks.
  void onKey(int key, int action);
  void onMouseButton(int button, int action);
  void onCursorPos(double x, double y);
  void onScroll(double dy);

 private:
  static constexpr int kMaxKeys = 512;  // > GLFW_KEY_LAST (348)
  static constexpr int kMaxButtons = 8;

  std::array<bool, kMaxKeys> m_keys{};
  std::array<bool, kMaxKeys> m_keysPressed{};
  std::array<bool, kMaxButtons> m_buttons{};
  glm::vec2 m_mousePos{0.0f};
  glm::vec2 m_mousePrevPos{0.0f};
  float m_scroll = 0.0f;
};

}  // namespace glint
