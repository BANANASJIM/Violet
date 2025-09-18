#include "Buffer.hpp"
#include "VulkanContext.hpp"
#include <stdexcept>

namespace violet {

uint32_t findMemoryType(VulkanContext* context, uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
    vk::PhysicalDeviceMemoryProperties memProperties = context->getPhysicalDevice().getMemoryProperties();

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type!");
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
    vk::CommandBuffer commandBuffer = beginSingleTimeCommands(context);

    vk::BufferCopy copyRegion;
    copyRegion.size = size;
    commandBuffer.copyBuffer(srcBuffer, dstBuffer, 1, &copyRegion);

    endSingleTimeCommands(context, commandBuffer);
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
    vk::raii::CommandBuffer commandBuffer = beginSingleTimeCommandsRAII(context);

    vk::BufferCopy copyRegion;
    copyRegion.size = size;
    commandBuffer.copyBuffer(*srcBuffer, *dstBuffer, copyRegion);

    endSingleTimeCommands(context, commandBuffer);
}

vk::CommandBuffer beginSingleTimeCommands(VulkanContext* context) {
    vk::CommandBufferAllocateInfo allocInfo;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = context->getCommandPool();
    allocInfo.commandBufferCount = 1;

    auto commandBuffers = context->getDevice().allocateCommandBuffers(allocInfo);
    vk::CommandBuffer commandBuffer = commandBuffers[0];

    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    commandBuffer.begin(beginInfo);

    return commandBuffer;
}

void endSingleTimeCommands(VulkanContext* context, vk::CommandBuffer commandBuffer) {
    commandBuffer.end();

    vk::SubmitInfo submitInfo;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    context->getGraphicsQueue().submit(1, &submitInfo, {});
    context->getGraphicsQueue().waitIdle();

    context->getDevice().freeCommandBuffers(context->getCommandPool(), 1, &commandBuffer);
}

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

void endSingleTimeCommands(VulkanContext* context, const vk::raii::CommandBuffer& commandBuffer) {
    commandBuffer.end();

    vk::SubmitInfo submitInfo;
    submitInfo.commandBufferCount = 1;
    vk::CommandBuffer cmdBuf = *commandBuffer;
    submitInfo.pCommandBuffers = &cmdBuf;

    context->getGraphicsQueue().submit(1, &submitInfo, {});
    context->getGraphicsQueue().waitIdle();
    // RAII CommandBuffer will automatically free itself
}

}