#include "UniformBuffer.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "core/Log.hpp"

namespace violet {

void UniformBuffer::create(VulkanContext* ctx, size_t size) {
    context = ctx;

    BufferInfo bufferInfo;
    bufferInfo.size = size;
    bufferInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer;
    bufferInfo.memoryUsage = MemoryUsage::CPU_TO_GPU;
    bufferInfo.debugName = "Uniform Buffer";

    bufferResource = ResourceFactory::createBuffer(ctx, bufferInfo);
    allocation = bufferResource.allocation;

    // Buffer is automatically mapped due to CPU_TO_GPU usage
}

void UniformBuffer::cleanup() {
    if (context) {
        ResourceFactory::destroyBuffer(context, bufferResource);
        allocation = VK_NULL_HANDLE;
        context = nullptr;
    }
}

void UniformBuffer::update(const void* data, size_t size) {
    if (bufferResource.mappedData && size <= bufferResource.size) {
        memcpy(bufferResource.mappedData, data, size);
    }
}

}