#pragma once

// The one include for Vulkan in the VK backend, plus VK_CHECK.

#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstdlib>

// Every VkResult-returning call goes through this. Failures are programming or environment errors we
// can't recover from here, so: print file/line/call/result and abort (a debugger stops right there).
// (Expected non-success results — VK_SUBOPTIMAL_KHR, VK_ERROR_OUT_OF_DATE_KHR — are checked explicitly.)
#define VK_CHECK(expr)                                                                                \
  do {                                                                                                \
    const VkResult vkCheckResult_ = (expr);                                                           \
    if (vkCheckResult_ != VK_SUCCESS) {                                                               \
      std::fprintf(stderr, "%s:%d: %s -> %s\n", __FILE__, __LINE__, #expr, string_VkResult(vkCheckResult_)); \
      std::abort();                                                                                   \
    }                                                                                                 \
  } while (0)
