#include "gl/debug.h"

#include "core/log.h"
#include "gl/gl.h"

namespace glint::gl {

namespace {

int g_issueCount = 0;

const char* sourceName(GLenum source) {
  switch (source) {
    case GL_DEBUG_SOURCE_API: return "API";
    case GL_DEBUG_SOURCE_WINDOW_SYSTEM: return "window system";
    case GL_DEBUG_SOURCE_SHADER_COMPILER: return "shader compiler";
    case GL_DEBUG_SOURCE_THIRD_PARTY: return "third party";
    case GL_DEBUG_SOURCE_APPLICATION: return "application";
    default: return "other";
  }
}

const char* typeName(GLenum type) {
  switch (type) {
    case GL_DEBUG_TYPE_ERROR: return "error";
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: return "deprecated";
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: return "undefined behaviour";
    case GL_DEBUG_TYPE_PORTABILITY: return "portability";
    case GL_DEBUG_TYPE_PERFORMANCE: return "performance";
    case GL_DEBUG_TYPE_MARKER: return "marker";
    default: return "other";
  }
}

void GLAD_API_PTR onDebugMessage(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei /*length*/,
                                 const GLchar* message, const void* /*user*/) {
  // NVIDIA chatter about buffer placement ("will use VIDEO memory as the source...") etc.
  if (id == 131185 || id == 131218 || id == 131204) return;

  const auto text = [&] { return fmt::format("GL {} {} #{}: {}", sourceName(source), typeName(type), id, message); };
  // Compile errors also come back through glGetShaderInfoLog, where gl::Program reports them with the file
  // name. Don't count them: a typo during hot reload isn't an API usage error.
  if (source == GL_DEBUG_SOURCE_SHADER_COMPILER) {
    log::debug("{}", text());
    return;
  }
  switch (severity) {
    case GL_DEBUG_SEVERITY_HIGH:
      ++g_issueCount;
      log::error("{}", text());
      break;
    case GL_DEBUG_SEVERITY_MEDIUM:
      ++g_issueCount;
      log::warn("{}", text());
      break;
    case GL_DEBUG_SEVERITY_LOW: log::info("{}", text()); break;
    default: log::debug("{}", text()); break;
  }
}

}  // namespace

void logContextInfo() {
  log::info("GL_VENDOR   {}", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
  log::info("GL_RENDERER {}", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
  log::info("GL_VERSION  {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
  log::info("GLSL        {}", reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)));
}

void enableDebugOutput() {
  GLint flags = 0;
  glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
  if (!(flags & GL_CONTEXT_FLAG_DEBUG_BIT)) log::warn("not a debug context: KHR_debug may report little or nothing");

  glEnable(GL_DEBUG_OUTPUT);
  // Synchronous: the callback runs inside the offending gl* call, so a breakpoint there shows the culprit.
  glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
  glDebugMessageCallback(onDebugMessage, nullptr);
  glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
  // Notifications (object created, shader stats, ...) are noise at this level.
  glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
  log::info("KHR_debug output enabled");
}

int debugIssueCount() { return g_issueCount; }

const char* rendererName() { return reinterpret_cast<const char*>(glGetString(GL_RENDERER)); }

}  // namespace glint::gl
