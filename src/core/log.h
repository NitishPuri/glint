#pragma once

// Logging on top of {fmt} (GCC 11 has no std::format).
//   glint::log::info("loaded {} ({} vertices)", path, mesh.vertexCount());
// Every line goes to the console (warn/error to stderr) and, once setFile() is called, to a file.

#include <fmt/format.h>

#include <glm/glm.hpp>
#include <string_view>

namespace glint::log {

enum class Level { Debug, Info, Warn, Error };

void write(Level level, std::string_view message);

// Also append every message to this file (truncated on open). Empty path closes it.
void setFile(const std::string& path);

// Messages below this level are dropped. Default: Debug in debug builds, Info otherwise.
void setMinLevel(Level level);

template <typename... Args>
void debug(fmt::format_string<Args...> f, Args&&... args) {
  write(Level::Debug, fmt::format(f, std::forward<Args>(args)...));
}
template <typename... Args>
void info(fmt::format_string<Args...> f, Args&&... args) {
  write(Level::Info, fmt::format(f, std::forward<Args>(args)...));
}
template <typename... Args>
void warn(fmt::format_string<Args...> f, Args&&... args) {
  write(Level::Warn, fmt::format(f, std::forward<Args>(args)...));
}
template <typename... Args>
void error(fmt::format_string<Args...> f, Args&&... args) {
  write(Level::Error, fmt::format(f, std::forward<Args>(args)...));
}

}  // namespace glint::log

// fmt support for glm vectors, e.g. log::info("eye {}", camera.eye) -> "eye (4, 3, -3)".
template <glm::length_t L, typename T, glm::qualifier Q>
struct fmt::formatter<glm::vec<L, T, Q>> : fmt::formatter<T> {
  auto format(const glm::vec<L, T, Q>& v, fmt::format_context& ctx) const {
    auto out = fmt::format_to(ctx.out(), "(");
    for (glm::length_t i = 0; i < L; ++i) {
      if (i > 0) out = fmt::format_to(out, ", ");
      ctx.advance_to(out);
      out = fmt::formatter<T>::format(v[i], ctx);
    }
    return fmt::format_to(out, ")");
  }
};
