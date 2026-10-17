#pragma once

// 15_prefix_sum — shared by gl.cpp and vk.cpp. Stream compaction built on a parallel prefix sum (scan):
//   1. flags:   point i of a jittered grid -> 1 if it lies inside an animated star, else 0
//   2. scan:    exclusive prefix sum of the flags = each selected point's slot in the output
//               (multi-level: scan 512-element blocks, scan the block sums, add them back)
//   3. scatter: selected point i writes its index to compact[slot]; the last thread writes the count
//   4. draw:    all points dimmed, then the compacted ones colored by slot (a smooth gradient = order kept)
// Read: GPU Gems 3 ch39 "Parallel Prefix Sum (Scan) with CUDA" — the shared-memory kernel here is its
// work-efficient (Blelloch) scan; the subgroup kernel is the modern hardware shortcut.

#include <fmt/format.h>
#include <imgui.h>

#include <cmath>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace glint::prefix_sum {

inline constexpr uint32_t kBlock = 512;           // elements scanned per workgroup (256 threads x 2)
inline constexpr uint32_t kThreads = 256;         // local_size_x of every kernel
inline constexpr uint32_t kMaxPoints = 1u << 22;  // 4M
inline constexpr const char* kKernels[] = {"shared memory (Blelloch, GG3 ch39)", "subgroup arithmetic"};

struct Params {
  int countLog2 = 18;
  int kernel = 1;  // index into kKernels
  bool showRejected = true;
  bool paused = false;
  bool verify = false;  // one-shot: read everything back and check it on the CPU

  uint32_t count() const { return 1u << countLog2; }
  // Grid width: points sit on a W x H grid in index order (row-major), so the compacted order — which a scan
  // preserves — shows up as a bottom-to-top color gradient.
  uint32_t gridWidth() const { return 1u << ((countLog2 + 1) / 2); }
};

// GPU time of each phase, from timestamps, in ms (filled by each API's query code).
struct Timings {
  double flags = 0, scan = 0, scatter = 0;
};

// Means over a run, logged when the technique is destroyed (like the app's per-technique `summary` line).
// Samples start after the startup checks (frame 30), so the auto-verify kernel switches don't mix in.
struct TimingLog {
  Timings sum;
  int samples = 0;
  void add(const Timings& t, int frame) {
    if (frame < 30 || t.scan == 0) return;
    sum = {sum.flags + t.flags, sum.scan + t.scan, sum.scatter + t.scatter};
    ++samples;
  }
  std::string summary(const Params& p) const {
    if (!samples) return "no samples";
    const double s = samples;
    return fmt::format("{} points, kernel '{}': flags {:.3f} ms, scan {:.3f} ms ({:.2f} G elements/s), scatter "
                       "{:.3f} ms ({} frames)",
                       p.count(), kKernels[p.kernel], sum.flags / s, sum.scan / s, p.count() / (sum.scan / s * 1e6),
                       sum.scatter / s, samples);
  }
};

inline void paramsUi(Params& p, const Timings& t, const std::string& verifyResult, uint32_t subgroupSize) {
  ImGui::Text("%u points (grid %u x %u)", p.count(), p.gridWidth(), p.count() / p.gridWidth());
  ImGui::SliderInt("count (log2)", &p.countLog2, 10, 22);
  ImGui::Combo("scan kernel", &p.kernel, kKernels, IM_ARRAYSIZE(kKernels));
  ImGui::Text("subgroup size: %u", subgroupSize);
  ImGui::Checkbox("show rejected points", &p.showRejected);
  ImGui::Checkbox("paused", &p.paused);
  ImGui::SeparatorText("GPU time (timestamps)");
  ImGui::Text("flags   %.3f ms", t.flags);
  ImGui::Text("scan    %.3f ms  (%.2f G elements/s)", t.scan, t.scan > 0 ? p.count() / (t.scan * 1e6) : 0.0);
  ImGui::Text("scatter %.3f ms", t.scatter);
  if (ImGui::Button("verify on CPU")) p.verify = true;
  if (!verifyResult.empty()) ImGui::TextWrapped("%s", verifyResult.c_str());
}

// Every run checks both kernels once, logged: frame 10 with the shared-memory kernel, frame 11 with the subgroup
// one (if available), then the user's choice comes back. A one-time stall, and correctness proven in every log.
struct AutoVerify {
  int frame = 0;
  int savedKernel = 0;
  void step(Params& p, bool subgroupAvailable) {
    ++frame;
    if (frame == 10) {
      savedKernel = p.kernel;
      p.kernel = 0;
      p.verify = true;
    } else if (frame == 11 && subgroupAvailable) {
      p.kernel = 1;
      p.verify = true;
    } else if (frame == 12) {
      p.kernel = savedKernel;
    }
  }
};

// --- the scan levels ----------------------------------------------------------------------------------------
// All levels live in one storage buffer, one after the other: level 0 = the N values (scanned in place), level
// 1 = one sum per 512-block of level 0, and so on until a level fits in a single block. Its one total goes to
// the slot right after it.
struct ScanLevel {
  uint32_t count;   // elements at this level
  uint32_t offset;  // where they start in the scan buffer
  uint32_t blocks;  // workgroups to scan them = elements of the next level
};

inline std::vector<ScanLevel> scanLevels(uint32_t n) {
  std::vector<ScanLevel> levels;
  uint32_t offset = 0;
  for (;;) {
    const uint32_t blocks = (n + kBlock - 1) / kBlock;
    levels.push_back({n, offset, blocks});
    if (blocks == 1) break;
    offset += n;
    n = blocks;
  }
  return levels;
}

// Elements in the scan buffer for `n` values: every level + the final total.
inline uint32_t scanBufferElements(uint32_t n) {
  const ScanLevel last = scanLevels(n).back();
  return last.offset + last.count + 1;
}

// Where level k's block sums go.
inline uint32_t sumsOffset(const std::vector<ScanLevel>& levels, size_t k) {
  return k + 1 < levels.size() ? levels[k + 1].offset : levels[k].offset + levels[k].count;
}

// --- push constants (uvec4 + vec4: same layout in std140 and VK) ------------------------------------------
struct PassConstants {
  glm::uvec4 counts;  // x = element count, y = offset, z = sums offset, w = grid width
  glm::vec4 params;   // x = time, y = aspect, z = draw mode (0 all points dim, 1 compacted)
};

// --- CPU check ---------------------------------------------------------------------------------------------
// Given what the GPU produced — flags, the scanned level 0, and compact = [count, indices...] — recompute both
// on the CPU and report the first mismatch. Independent of the float inside-test: it checks the scan and the
// compaction of whatever flags the GPU chose.
inline std::string verify(const std::vector<uint32_t>& flags, const std::vector<uint32_t>& scanned,
                          const std::vector<uint32_t>& compact, uint32_t n) {
  uint32_t sum = 0;
  for (uint32_t i = 0; i < n; ++i) {
    if (scanned[i] != sum) return fmt::format("FAIL: scan[{}] = {}, expected {}", i, scanned[i], sum);
    if (flags[i] > 1) return fmt::format("FAIL: flag[{}] = {}", i, flags[i]);
    if (flags[i]) {
      if (compact[1 + sum] != i) return fmt::format("FAIL: compact[{}] = {}, expected {}", sum, compact[1 + sum], i);
      ++sum;
    }
  }
  if (compact[0] != sum) return fmt::format("FAIL: count {}, expected {}", compact[0], sum);
  return fmt::format("OK: {} points, {} selected ({:.1f}%), scan and compaction match the CPU", n, sum,
                     100.0 * sum / n);
}

}  // namespace glint::prefix_sum
