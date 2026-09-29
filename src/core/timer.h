#pragma once

// CPU-side timing only. GPU timings need API queries (GL_TIME_ELAPSED vs VK timestamps) and live in
// each backend (Phase 7).

#include <chrono>
#include <string>

#include "core/log.h"

namespace glint {

// Stopwatch. Frame loop: `float dt = timer.lap();`
class Timer {
 public:
  using Clock = std::chrono::steady_clock;

  // Seconds since construction or the last reset()/lap().
  double elapsed() const { return std::chrono::duration<double>(Clock::now() - m_start).count(); }
  void reset() { m_start = Clock::now(); }
  // elapsed() + reset() in one step.
  double lap() {
    const auto now = Clock::now();
    const double seconds = std::chrono::duration<double>(now - m_start).count();
    m_start = now;
    return seconds;
  }

 private:
  Clock::time_point m_start = Clock::now();
};

// Logs "<name> took X ms" when it goes out of scope.
class ScopedTimer {
 public:
  explicit ScopedTimer(std::string name) : m_name(std::move(name)) {}
  ~ScopedTimer() { log::debug("{} took {:.3f} ms", m_name, m_timer.elapsed() * 1000.0); }

  ScopedTimer(const ScopedTimer&) = delete;
  ScopedTimer& operator=(const ScopedTimer&) = delete;

 private:
  std::string m_name;
  Timer m_timer;
};

}  // namespace glint
