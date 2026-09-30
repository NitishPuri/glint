#include "vk/descriptors.h"

namespace glint::vk {

VkDescriptorSetLayout createSetLayout(VkDevice device, std::initializer_list<VkDescriptorSetLayoutBinding> bindings) {
  VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  info.bindingCount = uint32_t(bindings.size());
  info.pBindings = bindings.begin();
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &layout));
  return layout;
}

VkDescriptorPool createPool(VkDevice device, uint32_t maxSets, std::initializer_list<VkDescriptorPoolSize> sizes) {
  VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  info.maxSets = maxSets;
  info.poolSizeCount = uint32_t(sizes.size());
  info.pPoolSizes = sizes.begin();
  VkDescriptorPool pool = VK_NULL_HANDLE;
  VK_CHECK(vkCreateDescriptorPool(device, &info, nullptr, &pool));
  return pool;
}

std::vector<VkDescriptorSet> allocateSets(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout,
                                          uint32_t count) {
  const std::vector<VkDescriptorSetLayout> layouts(count, layout);
  VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  info.descriptorPool = pool;
  info.descriptorSetCount = count;
  info.pSetLayouts = layouts.data();
  std::vector<VkDescriptorSet> sets(count);
  VK_CHECK(vkAllocateDescriptorSets(device, &info, sets.data()));
  return sets;
}

void writeBuffer(VkDevice device, VkDescriptorSet set, uint32_t binding, VkBuffer buffer, VkDeviceSize range,
                 VkDescriptorType type) {
  const VkDescriptorBufferInfo bufferInfo{buffer, 0, range};
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = set;
  write.dstBinding = binding;
  write.descriptorCount = 1;
  write.descriptorType = type;
  write.pBufferInfo = &bufferInfo;
  vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void writeImage(VkDevice device, VkDescriptorSet set, uint32_t binding, VkImageView view, VkSampler sampler,
                VkImageLayout layout) {
  const VkDescriptorImageInfo imageInfo{sampler, view, layout};
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = set;
  write.dstBinding = binding;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = &imageInfo;
  vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

VkPipelineLayout createPipelineLayout(VkDevice device, std::initializer_list<VkDescriptorSetLayout> setLayouts,
                                      std::initializer_list<VkPushConstantRange> pushConstants) {
  VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  info.setLayoutCount = uint32_t(setLayouts.size());
  info.pSetLayouts = setLayouts.begin();
  info.pushConstantRangeCount = uint32_t(pushConstants.size());
  info.pPushConstantRanges = pushConstants.begin();
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VK_CHECK(vkCreatePipelineLayout(device, &info, nullptr, &layout));
  return layout;
}

}  // namespace glint::vk
