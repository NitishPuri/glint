#pragma once

// Descriptor plumbing, as 02 and 03 write it out raw: set layout, pool, sets, writes; plus pipeline layout.
// GL has no counterpart objects: a shader's `binding = N` is a global slot you bind to directly.

#include <initializer_list>
#include <vector>

#include "vk/vk.h"

namespace glint::vk {

// e.g. createSetLayout(device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
//                               {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}})
VkDescriptorSetLayout createSetLayout(VkDevice device, std::initializer_list<VkDescriptorSetLayoutBinding> bindings);

// Room for `maxSets` sets with (in total) the given descriptor counts.
VkDescriptorPool createPool(VkDevice device, uint32_t maxSets, std::initializer_list<VkDescriptorPoolSize> sizes);

std::vector<VkDescriptorSet> allocateSets(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout,
                                          uint32_t count);

// Point `binding` of `set` at a buffer range. `type` UNIFORM_BUFFER or UNIFORM_BUFFER_DYNAMIC (then `range`
// is the size of *one* element; the offset comes per draw from vkCmdBindDescriptorSets).
void writeBuffer(VkDevice device, VkDescriptorSet set, uint32_t binding, VkBuffer buffer, VkDeviceSize range,
                 VkDescriptorType type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

// Point `binding` of `set` at an image view + sampler (combined image sampler).
void writeImage(VkDevice device, VkDescriptorSet set, uint32_t binding, VkImageView view, VkSampler sampler,
                VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

VkPipelineLayout createPipelineLayout(VkDevice device, std::initializer_list<VkDescriptorSetLayout> setLayouts,
                                      std::initializer_list<VkPushConstantRange> pushConstants = {});

}  // namespace glint::vk
