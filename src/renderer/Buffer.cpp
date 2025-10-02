#include "Buffer.hpp"
#include "VulkanContext.hpp"
#include "core/Log.hpp"
#include <cassert>

namespace violet {

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