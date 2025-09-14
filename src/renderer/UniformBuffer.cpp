#include "UniformBuffer.hpp"
#include "VulkanContext.hpp"
#include "Buffer.hpp"

namespace violet {

void UniformBuffer::create(VulkanContext* ctx, size_t size) {
    context = ctx;
    bufferSize = size;

    createBuffer(ctx, size, vk::BufferUsageFlagBits::eUniformBuffer,
                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                 buffer, bufferMemory);

    mapped = ctx->getDevice().mapMemory(bufferMemory, 0, size);
}

void UniformBuffer::cleanup() {
    if (context && buffer) {
        if (mapped) {
            context->getDevice().unmapMemory(bufferMemory);
            mapped = nullptr;
        }
        context->getDevice().destroyBuffer(buffer);
        context->getDevice().freeMemory(bufferMemory);
        buffer = nullptr;
        bufferMemory = nullptr;
    }
}

void UniformBuffer::update(const void* data, size_t size) {
    if (mapped && size <= bufferSize) {
        memcpy(mapped, data, size);
    }
}

}