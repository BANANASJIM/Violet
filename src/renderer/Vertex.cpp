#include "Vertex.hpp"
#include "VulkanContext.hpp"
#include "Buffer.hpp"

namespace violet {

void VertexBuffer::create(VulkanContext* ctx, const eastl::vector<Vertex>& vertices) {
    context = ctx;

    vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    vk::Buffer stagingBuffer;
    vk::DeviceMemory stagingBufferMemory;
    createBuffer(ctx, bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                 stagingBuffer, stagingBufferMemory);

    void* data = ctx->getDevice().mapMemory(stagingBufferMemory, 0, bufferSize);
    memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
    ctx->getDevice().unmapMemory(stagingBufferMemory);

    createBuffer(ctx, bufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
                 vk::MemoryPropertyFlagBits::eDeviceLocal, buffer, bufferMemory);

    copyBuffer(ctx, stagingBuffer, buffer, bufferSize);

    ctx->getDevice().destroyBuffer(stagingBuffer);
    ctx->getDevice().freeMemory(stagingBufferMemory);
}

void VertexBuffer::create(VulkanContext* ctx, const eastl::vector<uint32_t>& indices) {
    context = ctx;
    indexCount = static_cast<uint32_t>(indices.size());

    vk::DeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    vk::Buffer stagingBuffer;
    vk::DeviceMemory stagingBufferMemory;
    createBuffer(ctx, bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                 stagingBuffer, stagingBufferMemory);

    void* data = ctx->getDevice().mapMemory(stagingBufferMemory, 0, bufferSize);
    memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
    ctx->getDevice().unmapMemory(stagingBufferMemory);

    createBuffer(ctx, bufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
                 vk::MemoryPropertyFlagBits::eDeviceLocal, buffer, bufferMemory);

    copyBuffer(ctx, stagingBuffer, buffer, bufferSize);

    ctx->getDevice().destroyBuffer(stagingBuffer);
    ctx->getDevice().freeMemory(stagingBufferMemory);
}

void VertexBuffer::createWithDeduplication(VulkanContext* ctx, const eastl::vector<Vertex>& inputVertices) {
    context = ctx;

    // Deduplicate vertices
    eastl::vector<Vertex> uniqueVertices;
    eastl::vector<uint32_t> indices;
    VertexDeduplicator::deduplicate(inputVertices, uniqueVertices, indices);

    // Create vertex buffer with unique vertices
    vk::DeviceSize vertexBufferSize = sizeof(Vertex) * uniqueVertices.size();

    vk::Buffer stagingBuffer;
    vk::DeviceMemory stagingBufferMemory;
    createBuffer(ctx, vertexBufferSize, vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                 stagingBuffer, stagingBufferMemory);

    void* data = ctx->getDevice().mapMemory(stagingBufferMemory, 0, vertexBufferSize);
    memcpy(data, uniqueVertices.data(), static_cast<size_t>(vertexBufferSize));
    ctx->getDevice().unmapMemory(stagingBufferMemory);

    createBuffer(ctx, vertexBufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
                 vk::MemoryPropertyFlagBits::eDeviceLocal, buffer, bufferMemory);

    copyBuffer(ctx, stagingBuffer, buffer, vertexBufferSize);

    ctx->getDevice().destroyBuffer(stagingBuffer);
    ctx->getDevice().freeMemory(stagingBufferMemory);

    // Create index buffer
    indexCount = static_cast<uint32_t>(indices.size());
    vk::DeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();

    vk::Buffer indexStagingBuffer;
    vk::DeviceMemory indexStagingBufferMemory;
    createBuffer(ctx, indexBufferSize, vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                 indexStagingBuffer, indexStagingBufferMemory);

    data = ctx->getDevice().mapMemory(indexStagingBufferMemory, 0, indexBufferSize);
    memcpy(data, indices.data(), static_cast<size_t>(indexBufferSize));
    ctx->getDevice().unmapMemory(indexStagingBufferMemory);

    // Note: We'll need a separate buffer for indices, for now we skip this
    // as the current implementation uses a single buffer per VertexBuffer instance

    ctx->getDevice().destroyBuffer(indexStagingBuffer);
    ctx->getDevice().freeMemory(indexStagingBufferMemory);
}

void VertexBuffer::cleanup() {
    if (context && buffer) {
        context->getDevice().destroyBuffer(buffer);
        context->getDevice().freeMemory(bufferMemory);
        buffer = nullptr;
        bufferMemory = nullptr;
    }
}

}