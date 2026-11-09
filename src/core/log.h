#pragma once

// Logging on top of std::format.
//   glint::log::info("loaded {} ({} vertices)", path, mesh.vertexCount());
// Every line goes to the console (warn/error to stderr) and, once a file is open, to that file too.
// Console lines carry seconds since start; file lines also carry the wall-clock time, so logs from
// different runs can be lined up (see core/run_log.h for the per-run file and its header).

#include <format>

#include <glm/glm.hpp>
#include <string>
#include <string_view>

namespace glint::log {

enum class Level { Debug, Info, Warn, Error };

void write(Level level, std::string_view message);

// Also write every message to this file (truncated on open). Empty path closes it. Returns false if the
// file can't be opened.
bool setFile(const std::string& path);

// Messages below this level are dropped. Default: Debug in debug builds, Info otherwise.
void setMinLevel(Level level);

template <typename... Args>
void debug(std::format_string<Args...> f, Args&&... args) {
  write(Level::Debug, std::format(f, std::forward<Args>(args)...));
}
template <typename... Args>
void info(std::format_string<Args...> f, Args&&... args) {
  write(Level::Info, std::format(f, std::forward<Args>(args)...));
}
template <typename... Args>
void warn(std::format_string<Args...> f, Args&&... args) {
  write(Level::Warn, std::format(f, std::forward<Args>(args)...));
}
template <typename... Args>
void error(std::format_string<Args...> f, Args&&... args) {
  write(Level::Error, std::format(f, std::forward<Args>(args)...));
}

}  // namespace glint::log

// std::format support for glm vectors, e.g. log::info("eye {}", camera.eye) -> "eye (4, 3, -3)".
template <glm::length_t L, typename T, glm::qualifier Q>
struct std::formatter<glm::vec<L, T, Q>> : std::formatter<T> {
  auto format(const glm::vec<L, T, Q>& v, std::format_context& ctx) const {
    auto out = std::format_to(ctx.out(), "(");
    for (glm::length_t i = 0; i < L; ++i) {
      if (i > 0) out = std::format_to(out, ", ");
      ctx.advance_to(out);
      out = std::formatter<T>::format(v[i], ctx);
    }
    return std::format_to(out, ")");
  }
};
