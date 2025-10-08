#include "IndexBuffer.hpp"
#include "renderer/vulkan/VulkanContext.hpp"

namespace violet {

void IndexBuffer::create(VulkanContext* ctx, const eastl::vector<uint32_t>& indices) {
    context = ctx;
    indexCount = static_cast<uint32_t>(indices.size());
    indexType = vk::IndexType::eUint32;

    if (indices.empty()) {
        return;
    }

    vk::DeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    // Create staging buffer
    BufferInfo stagingInfo;
    stagingInfo.size = bufferSize;
    stagingInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
    stagingInfo.memoryUsage = MemoryUsage::CPU_TO_GPU;
    stagingInfo.debugName = "Index staging buffer";

    BufferResource stagingBuffer = ResourceFactory::createBuffer(ctx, stagingInfo);

    void* data = ResourceFactory::mapBuffer(ctx, stagingBuffer);
    memcpy(data, indices.data(), static_cast<size_t>(bufferSize));

    // Create index buffer
    BufferInfo indexInfo;
    indexInfo.size = bufferSize;
    indexInfo.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer;
    indexInfo.memoryUsage = MemoryUsage::GPU_ONLY;
    indexInfo.debugName = "Index buffer (uint32)";

    bufferResource = ResourceFactory::createBuffer(ctx, indexInfo);
    allocation = bufferResource.allocation;

    ResourceFactory::copyBuffer(ctx, stagingBuffer, bufferResource, bufferSize);
    ResourceFactory::destroyBuffer(ctx, stagingBuffer);
}

void IndexBuffer::create(VulkanContext* ctx, const eastl::vector<uint16_t>& indices) {
    context = ctx;
    indexCount = static_cast<uint32_t>(indices.size());
    indexType = vk::IndexType::eUint16;

    if (indices.empty()) {
        return;
    }

    vk::DeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    // Create staging buffer
    BufferInfo stagingInfo;
    stagingInfo.size = bufferSize;
    stagingInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
    stagingInfo.memoryUsage = MemoryUsage::CPU_TO_GPU;
    stagingInfo.debugName = "Index staging buffer";

    BufferResource stagingBuffer = ResourceFactory::createBuffer(ctx, stagingInfo);

    void* data = ResourceFactory::mapBuffer(ctx, stagingBuffer);
    memcpy(data, indices.data(), static_cast<size_t>(bufferSize));

    // Create index buffer
    BufferInfo indexInfo;
    indexInfo.size = bufferSize;
    indexInfo.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer;
    indexInfo.memoryUsage = MemoryUsage::GPU_ONLY;
    indexInfo.debugName = "Index buffer (uint16)";

    bufferResource = ResourceFactory::createBuffer(ctx, indexInfo);
    allocation = bufferResource.allocation;

    ResourceFactory::copyBuffer(ctx, stagingBuffer, bufferResource, bufferSize);
    ResourceFactory::destroyBuffer(ctx, stagingBuffer);
}

void IndexBuffer::cleanup() {
    if (context) {
        ResourceFactory::destroyBuffer(context, bufferResource);
        allocation = VK_NULL_HANDLE;
        context = nullptr;
        indexCount = 0;
    }
}

} // namespace violet