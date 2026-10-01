#pragma once

// Move-only GL program built from GLSL files, with hot reload. Wraps what 01_triangle did by hand
// (glCreateShader/glShaderSource/glCompileShader/glCreateProgram/glAttachShader/glLinkProgram).
//
//   gl::Program program({"02_cube/shaders/cube.gl.vert", "02_cube/shaders/cube.gl.frag"});
//   program.reloadIfChanged();                   // in update(): recompiles when a file changed on disk
//   program.set(0, mvp);                         // explicit-location uniform (glProgramUniform*, DSA)
//
// Paths are relative to GLINT_SHADER_DIR (the techniques/ folder in the source tree), and the stage comes
// from the extension (.vert/.frag/.comp). `#include "file"` is resolved here (GL's compiler has no include
// support; glslc does it for VK): relative to the including file, then techniques/common/shaders. That is
// how shared GL/VK shaders pull in common/shaders/glint.glsl. The first build throws on errors; a failed
// *reload* logs the error and keeps the last good program, so a typo while editing doesn't kill the app.
// VK has no runtime GLSL compiler: its shaders are SPIR-V built by glslc, and a "reload" means rebuilding
// the whole pipeline object.

#include <filesystem>
#include <glm/glm.hpp>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "core/timer.h"
#include "gl/gl.h"

namespace glint::gl {

class Program {
 public:
  Program() = default;
  // `defines` is inserted right after each file's #version line, e.g. "#define LIGHTING_MODEL 1\n" — GL's
  // way of making variants of one source (10_specialization_constants). VK: specialization constants.
  Program(std::initializer_list<std::string> relativePaths, std::string defines = {});
  ~Program() { glDeleteProgram(m_id); }
  Program(Program&& other) noexcept { swap(other); }
  Program& operator=(Program&& other) noexcept {
    swap(other);
    return *this;
  }
  Program(const Program&) = delete;
  Program& operator=(const Program&) = delete;

  GLuint id() const { return m_id; }
  void use() const { glUseProgram(m_id); }

  // Checks the files' modification times (at most twice a second) and rebuilds if any changed.
  // Returns true if a new program was swapped in.
  bool reloadIfChanged();

  // Uniforms at explicit locations (layout(location = N) uniform ...). DSA: no need to glUseProgram first.
  void set(GLint location, int v) const { glProgramUniform1i(m_id, location, v); }
  void set(GLint location, float v) const { glProgramUniform1f(m_id, location, v); }
  void set(GLint location, const glm::vec2& v) const { glProgramUniform2fv(m_id, location, 1, &v.x); }
  void set(GLint location, const glm::vec3& v) const { glProgramUniform3fv(m_id, location, 1, &v.x); }
  void set(GLint location, const glm::vec4& v) const { glProgramUniform4fv(m_id, location, 1, &v.x); }
  void set(GLint location, const glm::mat3& m) const {
    glProgramUniformMatrix3fv(m_id, location, 1, GL_FALSE, &m[0][0]);
  }
  void set(GLint location, const glm::mat4& m) const {
    glProgramUniformMatrix4fv(m_id, location, 1, GL_FALSE, &m[0][0]);
  }

 private:
  struct Stage {
    std::filesystem::path path;
    std::filesystem::file_time_type modified;
  };

  // Compiles and links all stages; throws std::runtime_error with the driver's log on failure.
  GLuint build();
  void swap(Program& other) noexcept {
    std::swap(m_id, other.m_id);
    std::swap(m_stages, other.m_stages);
    std::swap(m_includes, other.m_includes);
    std::swap(m_defines, other.m_defines);
    std::swap(m_checkTimer, other.m_checkTimer);
  }

  GLuint m_id = 0;
  std::vector<Stage> m_stages;
  std::vector<Stage> m_includes;  // files pulled in by #include, also watched for hot reload
  std::string m_defines;
  Timer m_checkTimer;
};

}  // namespace glint::gl
