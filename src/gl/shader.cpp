#include "gl/shader.h"

#include <stdexcept>

#include "core/assets.h"
#include "core/log.h"

namespace glint::gl {

namespace {

GLenum stageFromExtension(const std::filesystem::path& path) {
  const std::string ext = path.extension().string();
  if (ext == ".vert") return GL_VERTEX_SHADER;
  if (ext == ".frag") return GL_FRAGMENT_SHADER;
  if (ext == ".comp") return GL_COMPUTE_SHADER;
  if (ext == ".geom") return GL_GEOMETRY_SHADER;
  throw std::runtime_error(fmt::format("{}: unknown shader stage extension", path.string()));
}

std::filesystem::file_time_type modifiedTime(const std::filesystem::path& path) {
  std::error_code ec;  // file briefly missing while an editor saves it: treat as "unchanged"
  const auto time = std::filesystem::last_write_time(path, ec);
  return ec ? std::filesystem::file_time_type{} : time;
}

}  // namespace

Program::Program(std::initializer_list<std::string> relativePaths) {
  for (const std::string& relative : relativePaths) {
    const std::filesystem::path path = std::filesystem::path(GLINT_SHADER_DIR) / relative;
    m_stages.push_back({path, modifiedTime(path)});
  }
  m_id = build();
}

GLuint Program::build() {
  std::vector<GLuint> shaders;
  auto cleanup = [&] {
    for (GLuint s : shaders) glDeleteShader(s);
  };

  for (const Stage& stage : m_stages) {
    const std::string source = readTextFile(stage.path);
    const char* text = source.c_str();
    const GLuint shader = glCreateShader(stageFromExtension(stage.path));
    shaders.push_back(shader);
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char info[4096];
      glGetShaderInfoLog(shader, sizeof(info), nullptr, info);
      cleanup();
      throw std::runtime_error(fmt::format("{}:\n{}", stage.path.string(), info));
    }
  }

  const GLuint program = glCreateProgram();
  for (GLuint s : shaders) glAttachShader(program, s);
  glLinkProgram(program);
  cleanup();  // the program keeps attached shaders alive until it's deleted itself
  GLint ok = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char info[4096];
    glGetProgramInfoLog(program, sizeof(info), nullptr, info);
    glDeleteProgram(program);
    throw std::runtime_error(fmt::format("link {}:\n{}", m_stages.front().path.string(), info));
  }

  const std::string label = m_stages.front().path.stem().string();  // "cube.gl"
  glObjectLabel(GL_PROGRAM, program, GLsizei(label.size()), label.data());
  return program;
}

bool Program::reloadIfChanged() {
  if (m_checkTimer.elapsed() < 0.5) return false;
  m_checkTimer.reset();

  std::string changed;
  for (Stage& stage : m_stages) {
    const auto time = modifiedTime(stage.path);
    if (time != stage.modified && time != std::filesystem::file_time_type{}) {
      stage.modified = time;
      changed += (changed.empty() ? "" : ", ") + stage.path.filename().string();
    }
  }
  if (changed.empty()) return false;

  try {
    const GLuint program = build();
    glDeleteProgram(m_id);
    m_id = program;
    log::info("reloaded program after edit to {}", changed);
    return true;
  } catch (const std::exception& e) {
    log::error("shader reload failed, keeping the previous program: {}", e.what());
    return false;
  }
}

}  // namespace glint::gl
