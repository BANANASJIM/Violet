#pragma once

#include <vulkan/vulkan.hpp>
#include "ResourceFactory.hpp"

namespace violet {

class VulkanContext;

vk::CommandBuffer beginSingleTimeCommands(VulkanContext* context);
vk::raii::CommandBuffer beginSingleTimeCommandsRAII(VulkanContext* context);
void endSingleTimeCommands(VulkanContext* context, vk::CommandBuffer commandBuffer);
void endSingleTimeCommands(VulkanContext* context, const vk::raii::CommandBuffer& commandBuffer);

// Legacy functions for backward compatibility - will be deprecated
uint32_t findMemoryType(VulkanContext* context, uint32_t typeFilter, vk::MemoryPropertyFlags properties);
void createBuffer(VulkanContext* context, vk::DeviceSize size, vk::BufferUsageFlags usage,
                  vk::MemoryPropertyFlags properties, vk::Buffer& buffer, vk::DeviceMemory& bufferMemory);
void createBuffer(VulkanContext* context, vk::DeviceSize size, vk::BufferUsageFlags usage,
                  vk::MemoryPropertyFlags properties, vk::raii::Buffer& buffer, vk::raii::DeviceMemory& bufferMemory);
void copyBuffer(VulkanContext* context, vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::DeviceSize size);
void copyBuffer(VulkanContext* context, const vk::raii::Buffer& srcBuffer, const vk::raii::Buffer& dstBuffer, vk::DeviceSize size);

}