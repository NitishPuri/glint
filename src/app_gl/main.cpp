// glint_gl — the OpenGL app: window + context, ImGui, technique registry, frame loop.
//
// Compare with app_vk/main.cpp. Almost everything VK does explicitly (device, swapchain, frames in flight,
// command buffers, synchronisation) happens here implicitly inside the GL driver and glfwSwapBuffers.

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <memory>
#include <stdexcept>

#include "core/assets.h"
#include "core/camera.h"
#include "core/config.h"
#include "core/log.h"
#include "core/run_log.h"
#include "core/timer.h"
#include "core/ui.h"
#include "core/window.h"
#include "gl/debug.h"
#include "gl/gl.h"
#include "gl/technique.h"

using namespace glint;

namespace {

// GL is one big global state machine shared by every technique and by ImGui. Put the state techniques
// might have changed back to GL's defaults when switching, so nothing leaks from one technique into the
// next. (VK has no equivalent: that state is baked into each pipeline object.)
void resetGlState() {
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glUseProgram(0);
  glBindVertexArray(0);
  glDisable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);
  glDisable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glFrontFace(GL_CCW);
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

std::unique_ptr<gl::Technique> createTechnique(const std::string& name, Camera& camera) {
  std::unique_ptr<gl::Technique> technique = gl::Registry::instance().create(name);
  if (!technique) throw std::runtime_error(fmt::format("unknown technique '{}'", name));
  resetGlState();
  try {
    technique->init();
  } catch (const std::exception& e) {
    log::error("{}: init failed: {}", name, e.what());
    return nullptr;
  }
  technique->setupCamera(camera);
  log::info("technique {}", name);
  return technique;
}

// Reads back the default framebuffer's back buffer: this is what's about to be presented.
void saveScreenshot(const std::string& path, glm::ivec2 size) {
  ImageData image;
  image.width = size.x;
  image.height = size.y;
  image.channels = 4;
  image.pixels.resize(size_t(size.x) * size.y * 4);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadBuffer(GL_BACK);
  glReadPixels(0, 0, size.x, size.y, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
  // GL's window origin is bottom-left, so row 0 is the bottom row; PNG wants the top row first.
  writePng(path, image, /*flipVertically*/ true);
  log::info("screenshot saved to {}", path);
}

int run(const Config& config) {
  Window window({.title = "glint - OpenGL",
                 .width = config.width,
                 .height = config.height,
                 .api = GraphicsApi::OpenGL,
                 .glDebugContext = config.validation});

  // GL: the context was created with the window; make it current on this thread and load the function
  // pointers (everything past GL 1.1 is fetched from the driver at runtime).
  glfwMakeContextCurrent(window.handle());
  if (!gladLoadGL(glfwGetProcAddress)) throw std::runtime_error("gladLoadGL failed");
  glfwSwapInterval(config.vsync ? 1 : 0);  // VK: choosing FIFO vs IMMEDIATE/MAILBOX present mode
  gl::logContextInfo();
  if (config.validation) gl::enableDebugOutput();

  ui::init();
  ImGui_ImplGlfw_InitForOpenGL(window.handle(), /*install_callbacks*/ true);
  ImGui_ImplOpenGL3_Init("#version 460");

  const std::vector<std::string> names = gl::Registry::instance().names();
  if (names.empty()) throw std::runtime_error("no GL techniques registered");
  std::string current = config.technique.empty() ? names.front() : config.technique;
  Camera camera;
  std::unique_ptr<gl::Technique> technique = createTechnique(current, camera);

  Timer frameTimer;
  const Timer startTimer;
  ui::FrameStats stats;
  // For the run log: frame times and debug issues while the current technique was on screen.
  FrameTimeSummary summary;
  int issuesAtStart = gl::debugIssueCount();
  auto logSummary = [&] {
    if (summary.count() == 0) return;
    log::info("summary  {}: {}, KHR_debug issues {}", current, summary.describe(),
              gl::debugIssueCount() - issuesAtStart);
    summary.clear();
    issuesAtStart = gl::debugIssueCount();
  };
  const std::string device = gl::rendererName();

  for (uint64_t frameIndex = 0; !window.shouldClose(); ++frameIndex) {
    window.pollEvents();
    if (window.isMinimized()) {
      window.waitWhileMinimized();
      frameTimer.reset();
      continue;
    }
    const float dt = float(frameTimer.lap());
    stats.add(dt);
    summary.add(dt);
    const glm::ivec2 fbSize = window.framebufferSize();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ui::newFrame(window.input());

    camera.update(dt, window.input(), fbSize);
    Frame frame{dt, startTimer.elapsed(), frameIndex, fbSize, camera, window.input()};

    if (technique) {
      technique->update(dt, frame);
      technique->render(frame);
    } else {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, fbSize.x, fbSize.y);
      glClearColor(0.3f, 0.05f, 0.05f, 1.0f);  // dark red: the technique failed to initialise
      glClear(GL_COLOR_BUFFER_BIT);
    }

    const bool lastFrame = config.exitAfterFrames > 0 && frameIndex + 1 >= uint64_t(config.exitAfterFrames);
    if (lastFrame && !config.screenshot.empty()) saveScreenshot(config.screenshot, fbSize);  // before the UI

    std::optional<std::string> picked;
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("glint - OpenGL");
    picked = ui::techniqueCombo(names, current);
    ui::statsSection(stats, "OpenGL 4.6", device, fbSize.x, fbSize.y);
    if (config.validation) ImGui::Text("KHR_debug issues: %d", gl::debugIssueCount());
    ui::cameraSection(camera);
    if (technique && ImGui::CollapsingHeader(current.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) technique->ui();
    ImGui::End();
    ImGui::Render();

    // ImGui's GL backend saves and restores the state it touches, but it draws into whatever framebuffer
    // and viewport are current.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fbSize.x, fbSize.y);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // GL: present. The driver handles buffering, vsync and CPU/GPU synchronisation behind this call.
    // VK: vkQueuePresentKHR after acquire/submit, with semaphores and fences managed by the app.
    glfwSwapBuffers(window.handle());

    if (picked) {
      logSummary();
      technique.reset();  // destroy the old technique's GL objects first
      current = *picked;
      technique = createTechnique(current, camera);
    }
    if (lastFrame) window.requestClose();
  }

  logSummary();
  technique.reset();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ui::shutdown();
  if (config.validation) log::info("KHR_debug issues (severity >= medium): {}", gl::debugIssueCount());
  return config.validation && gl::debugIssueCount() > 0 ? 2 : 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Config config = parseArgs(argc, argv);
    if (config.showHelp) {
      fmt::print("{}", usage(argv[0]));
      fmt::print("techniques:\n");
      for (const std::string& name : gl::Registry::instance().names()) fmt::print("  {}\n", name);
      return 0;
    }
    startRunLog("glint_gl", config, argc, argv);
    if (!config.assetDir.empty()) setAssetDir(config.assetDir);
    return run(config);
  } catch (const std::exception& e) {
    log::error("{}", e.what());
    return 1;
  }
}
