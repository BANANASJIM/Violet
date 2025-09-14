#pragma once

#include <vulkan/vulkan.hpp>

namespace violet {

class VulkanContext;

uint32_t findMemoryType(VulkanContext* context, uint32_t typeFilter, vk::MemoryPropertyFlags properties);

void createBuffer(VulkanContext* context, vk::DeviceSize size, vk::BufferUsageFlags usage,
                  vk::MemoryPropertyFlags properties, vk::Buffer& buffer, vk::DeviceMemory& bufferMemory);

void copyBuffer(VulkanContext* context, vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::DeviceSize size);

vk::CommandBuffer beginSingleTimeCommands(VulkanContext* context);
void endSingleTimeCommands(VulkanContext* context, vk::CommandBuffer commandBuffer);

}