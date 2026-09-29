#include "core/window.h"

#include <GLFW/glfw3.h>

#include <stdexcept>

#include "core/log.h"

namespace glint {

namespace {

Window* fromHandle(GLFWwindow* w) { return static_cast<Window*>(glfwGetWindowUserPointer(w)); }

}  // namespace

Window::Window(const WindowDesc& desc) : m_api(desc.api) {
  glfwSetErrorCallback([](int code, const char* message) { log::error("GLFW error {}: {}", code, message); });
  if (!glfwInit()) throw std::runtime_error("glfwInit failed");

  glfwDefaultWindowHints();
  if (desc.api == GraphicsApi::OpenGL) {
    // GL: the window *is* the context. Ask for 4.6 core; GLFW fails window creation if the driver can't.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, desc.glDebugContext ? GLFW_TRUE : GLFW_FALSE);
  } else {
    // VK: no context at all. The app creates a VkSurfaceKHR for this window and a swapchain on top of it.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  }

  m_window = glfwCreateWindow(desc.width, desc.height, desc.title.c_str(), nullptr, nullptr);
  if (!m_window) {
    glfwTerminate();
    throw std::runtime_error("glfwCreateWindow failed (see GLFW error above)");
  }

  // These callbacks must be installed *before* ImGui_ImplGlfw_Init(window, true): ImGui saves the
  // previous callbacks and chains to them, so both ImGui and Input see every event.
  glfwSetWindowUserPointer(m_window, this);
  glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* w, int, int) { fromHandle(w)->m_resized = true; });
  glfwSetKeyCallback(m_window, [](GLFWwindow* w, int key, int, int action, int) {
    fromHandle(w)->m_input.onKey(key, action);
  });
  glfwSetMouseButtonCallback(m_window, [](GLFWwindow* w, int button, int action, int) {
    fromHandle(w)->m_input.onMouseButton(button, action);
  });
  glfwSetCursorPosCallback(m_window, [](GLFWwindow* w, double x, double y) {
    fromHandle(w)->m_input.onCursorPos(x, y);
  });
  glfwSetScrollCallback(m_window, [](GLFWwindow* w, double, double dy) { fromHandle(w)->m_input.onScroll(dy); });

  double x = 0, y = 0;
  glfwGetCursorPos(m_window, &x, &y);
  m_input.onCursorPos(x, y);
  m_input.beginFrame();

  const glm::ivec2 fb = framebufferSize();
  log::info("window {}x{} (framebuffer {}x{}) for {}", desc.width, desc.height, fb.x, fb.y,
            desc.api == GraphicsApi::OpenGL ? "OpenGL" : "Vulkan");
}

Window::~Window() {
  glfwDestroyWindow(m_window);
  glfwTerminate();
}

bool Window::shouldClose() const { return glfwWindowShouldClose(m_window); }

void Window::requestClose() { glfwSetWindowShouldClose(m_window, GLFW_TRUE); }

void Window::pollEvents() {
  m_input.beginFrame();
  glfwPollEvents();
  if (m_input.keyPressed(GLFW_KEY_ESCAPE)) requestClose();
}

glm::ivec2 Window::framebufferSize() const {
  glm::ivec2 size{0};
  glfwGetFramebufferSize(m_window, &size.x, &size.y);
  return size;
}

bool Window::isMinimized() const {
  const glm::ivec2 size = framebufferSize();
  return size.x == 0 || size.y == 0;
}

void Window::waitWhileMinimized() {
  while (isMinimized() && !shouldClose()) glfwWaitEvents();
}

bool Window::consumeResized() {
  const bool resized = m_resized;
  m_resized = false;
  return resized;
}

void Window::setTitle(const std::string& title) { glfwSetWindowTitle(m_window, title.c_str()); }

}  // namespace glint
