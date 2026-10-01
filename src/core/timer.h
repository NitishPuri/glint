#pragma once

// CPU-side timing only. GPU timings need API queries (GL_TIME_ELAPSED vs VK timestamps) and live in
// each backend (Phase 7).

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

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

// Every frame time of a stretch of frames (e.g. one technique's time on screen), summarised once at the
// end for the run log: "01_triangle: 1234 frames, mean 6.94 ms, p95 7.10 ms, max 12.3 ms".
// Keeps all samples (4 bytes a frame: ~2 MB for an hour at 144 Hz), so percentiles are exact.
class FrameTimeSummary {
 public:
  void add(float dtSeconds) { m_ms.push_back(dtSeconds * 1000.0f); }
  void clear() { m_ms.clear(); }
  size_t count() const { return m_ms.size(); }

  // "mean 1.23 ms, p95 2.34 ms" (or "-" if empty): for secondary series like CPU/GPU time.
  std::string meanP95() const {
    if (m_ms.empty()) return "-";
    std::vector<float> sorted = m_ms;
    std::sort(sorted.begin(), sorted.end());
    double sum = 0.0;
    for (float ms : sorted) sum += ms;
    const float p95 = sorted[std::min(sorted.size() - 1, size_t(double(sorted.size()) * 0.95))];
    return fmt::format("mean {:.3f} ms, p95 {:.3f} ms", sum / double(sorted.size()), p95);
  }

  // Empty string if no frames were recorded.
  std::string describe() const {
    if (m_ms.empty()) return {};
    std::vector<float> sorted = m_ms;
    std::sort(sorted.begin(), sorted.end());
    double sum = 0.0;
    for (float ms : sorted) sum += ms;
    const float p95 = sorted[std::min(sorted.size() - 1, size_t(double(sorted.size()) * 0.95))];
    return fmt::format("{} frames, mean {:.2f} ms, p95 {:.2f} ms, max {:.2f} ms", sorted.size(),
                       sum / double(sorted.size()), p95, sorted.back());
  }

 private:
  std::vector<float> m_ms;
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
