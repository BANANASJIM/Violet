#include "IndexBuffer.hpp"
#include "VulkanContext.hpp"
#include "Buffer.hpp"

namespace violet {

void IndexBuffer::create(VulkanContext* ctx, const eastl::vector<uint32_t>& indices) {
    context = ctx;
    indexCount = static_cast<uint32_t>(indices.size());
    indexType = vk::IndexType::eUint32;

    if (indices.empty()) {
        return;
    }

    vk::DeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    vk::Buffer stagingBuffer;
    vk::DeviceMemory stagingBufferMemory;
    createBuffer(context, bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                 stagingBuffer, stagingBufferMemory);

    void* data = context->getDevice().mapMemory(stagingBufferMemory, 0, bufferSize);
    memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
    context->getDevice().unmapMemory(stagingBufferMemory);

    createBuffer(context, bufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
                 vk::MemoryPropertyFlagBits::eDeviceLocal, buffer, bufferMemory);

    copyBuffer(context, stagingBuffer, buffer, bufferSize);

    context->getDevice().destroyBuffer(stagingBuffer);
    context->getDevice().freeMemory(stagingBufferMemory);
}

void IndexBuffer::create(VulkanContext* ctx, const eastl::vector<uint16_t>& indices) {
    context = ctx;
    indexCount = static_cast<uint32_t>(indices.size());
    indexType = vk::IndexType::eUint16;

    if (indices.empty()) {
        return;
    }

    vk::DeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    vk::Buffer stagingBuffer;
    vk::DeviceMemory stagingBufferMemory;
    createBuffer(context, bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                 stagingBuffer, stagingBufferMemory);

    void* data = context->getDevice().mapMemory(stagingBufferMemory, 0, bufferSize);
    memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
    context->getDevice().unmapMemory(stagingBufferMemory);

    createBuffer(context, bufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
                 vk::MemoryPropertyFlagBits::eDeviceLocal, buffer, bufferMemory);

    copyBuffer(context, stagingBuffer, buffer, bufferSize);

    context->getDevice().destroyBuffer(stagingBuffer);
    context->getDevice().freeMemory(stagingBufferMemory);
}

void IndexBuffer::cleanup() {
    if (context && buffer) {
        context->getDevice().destroyBuffer(buffer);
        context->getDevice().freeMemory(bufferMemory);
        buffer = nullptr;
        bufferMemory = nullptr;
        context = nullptr;
        indexCount = 0;
    }
}

} // namespace violet