#include "resource/Vertex.hpp"
#include "renderer/core/VulkanContext.hpp"
#include "resource/gpu/Buffer.hpp"

namespace violet {

void VertexBuffer::create(VulkanContext* ctx, const eastl::vector<Vertex>& vertices) {
    context = ctx;

    vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    // Create staging buffer
    BufferInfo stagingInfo;
    stagingInfo.size = bufferSize;
    stagingInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
    stagingInfo.memoryUsage = MemoryUsage::CPU_TO_GPU;
    stagingInfo.debugName = "Vertex staging buffer";

    BufferResource stagingBuffer = ResourceFactory::createBuffer(ctx, stagingInfo);

    void* data = ResourceFactory::mapBuffer(ctx, stagingBuffer);
    memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));

    // Create vertex buffer
    BufferInfo vertexInfo;
    vertexInfo.size = bufferSize;
    vertexInfo.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer;
    vertexInfo.memoryUsage = MemoryUsage::GPU_ONLY;
    vertexInfo.debugName = "Vertex buffer";

    bufferResource = ResourceFactory::createBuffer(ctx, vertexInfo);
    allocation = bufferResource.allocation;

    ResourceFactory::copyBuffer(ctx, stagingBuffer, bufferResource, bufferSize);
    ResourceFactory::destroyBuffer(ctx, stagingBuffer);
}

void VertexBuffer::create(VulkanContext* ctx, const eastl::vector<uint32_t>& indices) {
    context = ctx;
    indexCount = static_cast<uint32_t>(indices.size());

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
    indexInfo.debugName = "Index buffer";

    bufferResource = ResourceFactory::createBuffer(ctx, indexInfo);
    allocation = bufferResource.allocation;

    ResourceFactory::copyBuffer(ctx, stagingBuffer, bufferResource, bufferSize);
    ResourceFactory::destroyBuffer(ctx, stagingBuffer);
}

void VertexBuffer::createWithDeduplication(VulkanContext* ctx, const eastl::vector<Vertex>& inputVertices) {
    context = ctx;

    // Deduplicate vertices
    eastl::vector<Vertex> uniqueVertices;
    eastl::vector<uint32_t> indices;
    VertexDeduplicator::deduplicate(inputVertices, uniqueVertices, indices);

    // Create vertex buffer with unique vertices
    vk::DeviceSize vertexBufferSize = sizeof(Vertex) * uniqueVertices.size();

    // Create staging buffer for vertices
    BufferInfo stagingInfo;
    stagingInfo.size = vertexBufferSize;
    stagingInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
    stagingInfo.memoryUsage = MemoryUsage::CPU_TO_GPU;
    stagingInfo.debugName = "Vertex dedup staging buffer";

    BufferResource stagingBuffer = ResourceFactory::createBuffer(ctx, stagingInfo);

    void* data = ResourceFactory::mapBuffer(ctx, stagingBuffer);
    memcpy(data, uniqueVertices.data(), static_cast<size_t>(vertexBufferSize));

    // Create vertex buffer
    BufferInfo vertexInfo;
    vertexInfo.size = vertexBufferSize;
    vertexInfo.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer;
    vertexInfo.memoryUsage = MemoryUsage::GPU_ONLY;
    vertexInfo.debugName = "Deduplicated vertex buffer";

    bufferResource = ResourceFactory::createBuffer(ctx, vertexInfo);
    allocation = bufferResource.allocation;

    ResourceFactory::copyBuffer(ctx, stagingBuffer, bufferResource, vertexBufferSize);
    ResourceFactory::destroyBuffer(ctx, stagingBuffer);

    // Store index count for reference, but don't create index buffer here
    // as VertexBuffer only manages vertex data
    indexCount = static_cast<uint32_t>(indices.size());
}

void VertexBuffer::cleanup() {
    if (context) {
        ResourceFactory::destroyBuffer(context, bufferResource);
        allocation = VK_NULL_HANDLE;
        context = nullptr;
        indexCount = 0;
    }
}

}