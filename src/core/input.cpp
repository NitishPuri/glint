#include "core/input.h"

#include <GLFW/glfw3.h>

namespace glint {

bool Input::keyDown(int key) const { return key >= 0 && key < kMaxKeys && m_keys[key]; }

bool Input::keyPressed(int key) const { return key >= 0 && key < kMaxKeys && m_keysPressed[key]; }

bool Input::mouseDown(int button) const { return button >= 0 && button < kMaxButtons && m_buttons[button]; }

void Input::beginFrame() {
  m_keysPressed.fill(false);
  m_mousePrevPos = m_mousePos;
  m_scroll = 0.0f;
}

void Input::onKey(int key, int action) {
  if (key < 0 || key >= kMaxKeys) return;  // GLFW_KEY_UNKNOWN is -1
  if (action == GLFW_PRESS) {
    m_keys[key] = true;
    m_keysPressed[key] = true;
  } else if (action == GLFW_RELEASE) {
    m_keys[key] = false;
  }
}

void Input::onMouseButton(int button, int action) {
  if (button < 0 || button >= kMaxButtons) return;
  m_buttons[button] = action == GLFW_PRESS;
}

void Input::onCursorPos(double x, double y) { m_mousePos = {float(x), float(y)}; }

void Input::onScroll(double dy) { m_scroll += float(dy); }

}  // namespace glint
