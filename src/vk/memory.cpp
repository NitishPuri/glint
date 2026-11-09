#include "vk/memory.h"

#include <vk_mem_alloc.h>

#include <atomic>
#include <fstream>

#include "core/log.h"
#include "vk/context.h"

namespace glint::vk {

namespace {
std::atomic<long long> g_rawCount{0}, g_rawBytes{0};

std::string mb(double bytes) { return fmt::format("{:.1f} MB", bytes / (1024.0 * 1024.0)); }
}  // namespace

void trackRawAllocation(VkDeviceSize bytes, int delta) {
  g_rawCount += delta;
  g_rawBytes += delta * static_cast<long long>(bytes);
}

std::string memoryReport(const Context& ctx) {
  if (!ctx.allocator) {
    return fmt::format("raw: {} allocations = {} VkDeviceMemory objects, {}", g_rawCount.load(), g_rawCount.load(),
                       mb(double(g_rawBytes.load())));
  }
  VmaTotalStatistics stats;
  vmaCalculateStatistics(ctx.allocator, &stats);
  const VmaStatistics& t = stats.total.statistics;
  // blockCount includes dedicated allocations (a block holding exactly one resource). The JSON dump
  // (--vma-stats) shows each block's contents.
  return fmt::format("VMA: {} allocations in {} VkDeviceMemory blocks, {} used of {} allocated", t.allocationCount,
                     t.blockCount, mb(double(t.allocationBytes)), mb(double(t.blockBytes)));
}

void writeVmaStats(const Context& ctx, const std::string& path) {
  if (!ctx.allocator) return;
  char* json = nullptr;
  vmaBuildStatsString(ctx.allocator, &json, VK_TRUE);  // detailed: every allocation in every block
  std::ofstream(path) << json;
  vmaFreeStatsString(ctx.allocator, json);
  log::info("VMA statistics written to {}", path);
}

}  // namespace glint::vk
