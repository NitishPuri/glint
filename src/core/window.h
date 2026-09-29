#pragma once

// GLFW window, shared by both apps. It only chooses the GLFW hints for the API; everything after
// window creation is API-specific and lives in the app:
//   GL: glfwMakeContextCurrent, load the GL function pointers, glfwSwapInterval, glfwSwapBuffers
//   VK: glfwGetRequiredInstanceExtensions, glfwCreateWindowSurface, swapchain present
// A window created for one API can't be used with the other, which is why there are two executables.

#include <glm/glm.hpp>
#include <string>

#include "core/input.h"

struct GLFWwindow;

namespace glint {

enum class GraphicsApi { OpenGL, Vulkan };

struct WindowDesc {
  std::string title = "glint";
  int width = 1600;
  int height = 900;
  GraphicsApi api = GraphicsApi::OpenGL;
  bool glDebugContext = false;  // OpenGL only: request a KHR_debug-capable debug context
};

class Window {
 public:
  explicit Window(const WindowDesc& desc);
  ~Window();
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  GLFWwindow* handle() const { return m_window; }
  GraphicsApi api() const { return m_api; }

  bool shouldClose() const;
  void requestClose();
  // Input::beginFrame() + glfwPollEvents(). Escape closes the window.
  void pollEvents();

  // Size in pixels: what the viewport / swapchain extent must match (differs from the window size on HiDPI).
  glm::ivec2 framebufferSize() const;
  bool isMinimized() const;
  // Blocks in glfwWaitEvents() while the framebuffer is 0x0 (minimized). VK can't create a 0x0 swapchain.
  void waitWhileMinimized();
  // True once after the framebuffer size changed.
  bool consumeResized();

  Input& input() { return m_input; }
  const Input& input() const { return m_input; }

  void setTitle(const std::string& title);

 private:
  GLFWwindow* m_window = nullptr;
  GraphicsApi m_api;
  Input m_input;
  bool m_resized = false;
};

}  // namespace glint
