#include "Buffer.hpp"
#include "VulkanContext.hpp"
#include "core/Log.hpp"
#include <cassert>

namespace violet {

uint32_t findMemoryType(VulkanContext* context, uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
    vk::PhysicalDeviceMemoryProperties memProperties = context->getPhysicalDevice().getMemoryProperties();

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    violet::Log::critical("Renderer", "Failed to find suitable memory type!");
    assert(false && "Failed to find suitable memory type!");
    return 0;
}

void createBuffer(VulkanContext* context, vk::DeviceSize size, vk::BufferUsageFlags usage,
                  vk::MemoryPropertyFlags properties, vk::Buffer& buffer, vk::DeviceMemory& bufferMemory) {
    vk::BufferCreateInfo bufferInfo;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    buffer = context->getDevice().createBuffer(bufferInfo);

    vk::MemoryRequirements memRequirements = context->getDevice().getBufferMemoryRequirements(buffer);

    vk::MemoryAllocateInfo allocInfo;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(context, memRequirements.memoryTypeBits, properties);

    bufferMemory = context->getDevice().allocateMemory(allocInfo);
    context->getDevice().bindBufferMemory(buffer, bufferMemory, 0);
}

void copyBuffer(VulkanContext* context, vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::DeviceSize size) {
    ResourceFactory::executeSingleTimeCommands(context, [&](vk::CommandBuffer cmd) {
        vk::BufferCopy copyRegion;
        copyRegion.size = size;
        cmd.copyBuffer(srcBuffer, dstBuffer, 1, &copyRegion);
    });
}

void createBuffer(VulkanContext* context, vk::DeviceSize size, vk::BufferUsageFlags usage,
                  vk::MemoryPropertyFlags properties, vk::raii::Buffer& buffer, vk::raii::DeviceMemory& bufferMemory) {
    vk::BufferCreateInfo bufferInfo;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    buffer = vk::raii::Buffer(context->getDeviceRAII(), bufferInfo);

    vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();

    vk::MemoryAllocateInfo allocInfo;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(context, memRequirements.memoryTypeBits, properties);

    bufferMemory = vk::raii::DeviceMemory(context->getDeviceRAII(), allocInfo);
    buffer.bindMemory(*bufferMemory, 0);
}

void copyBuffer(VulkanContext* context, const vk::raii::Buffer& srcBuffer, const vk::raii::Buffer& dstBuffer, vk::DeviceSize size) {
    ResourceFactory::executeSingleTimeCommands(context, [&](vk::CommandBuffer cmd) {
        vk::BufferCopy copyRegion;
        copyRegion.size = size;
        cmd.copyBuffer(*srcBuffer, *dstBuffer, copyRegion);
    });
}

// RAII variant for special use cases
vk::raii::CommandBuffer beginSingleTimeCommandsRAII(VulkanContext* context) {
    vk::CommandBufferAllocateInfo allocInfo;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = context->getCommandPool();
    allocInfo.commandBufferCount = 1;

    vk::raii::CommandBuffers commandBuffers(context->getDeviceRAII(), allocInfo);
    vk::raii::CommandBuffer commandBuffer = std::move(commandBuffers[0]);

    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    commandBuffer.begin(beginInfo);

    return commandBuffer;
}

}