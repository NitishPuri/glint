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

// The file's text with every `#include "name"` line replaced by that file's (recursively resolved) text.
// #line directives keep compiler error line numbers pointing at the right lines of each file.
std::string loadWithIncludes(const std::filesystem::path& path, std::vector<std::filesystem::path>& included,
                             int depth = 0) {
  if (depth > 8) throw std::runtime_error(fmt::format("{}: #include nested too deep", path.string()));
  const std::string text = readTextFile(path);
  std::string out;
  size_t lineNumber = 0, start = 0;
  while (start < text.size()) {
    const size_t end = text.find('\n', start);
    const std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
    ++lineNumber;
    start = end == std::string::npos ? text.size() : end + 1;

    const size_t hash = line.find_first_not_of(" \t");
    if (hash != std::string::npos && line.compare(hash, 8, "#include") == 0) {
      const size_t open = line.find('"'), close = line.rfind('"');
      if (open == std::string::npos || close <= open) {
        throw std::runtime_error(fmt::format("{}:{}: bad #include", path.string(), lineNumber));
      }
      const std::string name = line.substr(open + 1, close - open - 1);
      std::filesystem::path file = path.parent_path() / name;
      if (!std::filesystem::exists(file)) file = std::filesystem::path(GLINT_SHADER_DIR) / "common/shaders" / name;
      included.push_back(file);
      out += "#line 1\n" + loadWithIncludes(file, included, depth + 1) + fmt::format("\n#line {}\n", lineNumber + 1);
    } else {
      out += line + "\n";
    }
  }
  return out;
}

}  // namespace

Program::Program(std::initializer_list<std::string> relativePaths, std::string defines)
    : m_defines(std::move(defines)) {
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

  std::vector<std::filesystem::path> included;
  for (const Stage& stage : m_stages) {
    std::string source = loadWithIncludes(stage.path, included);
    if (!m_defines.empty()) {
      // After the first line (#version must come first); #line keeps error line numbers matching the file.
      const size_t eol = source.find('\n');
      if (eol != std::string::npos) source.insert(eol + 1, m_defines + "#line 2\n");
    }
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

  m_includes.clear();
  for (const auto& file : included) {  // several stages often include the same file: watch it once
    bool seen = false;
    for (const Stage& existing : m_includes) seen = seen || existing.path == file;
    if (!seen) m_includes.push_back({file, modifiedTime(file)});
  }

  const std::string label = m_stages.front().path.stem().string();  // "cube.gl"
  glObjectLabel(GL_PROGRAM, program, GLsizei(label.size()), label.data());
  return program;
}

bool Program::reloadIfChanged() {
  if (m_checkTimer.elapsed() < 0.5) return false;
  m_checkTimer.reset();

  std::string changed;
  std::vector<Stage*> watched;
  for (Stage& stage : m_stages) watched.push_back(&stage);
  for (Stage& include : m_includes) watched.push_back(&include);
  for (Stage* entry : watched) {
    Stage& stage = *entry;
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
