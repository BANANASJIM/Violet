#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>

namespace violet {

class VulkanContext;

class IndexBuffer {
public:
    IndexBuffer() = default;
    ~IndexBuffer() { cleanup(); }

    // Delete copy operations
    IndexBuffer(const IndexBuffer&) = delete;
    IndexBuffer& operator=(const IndexBuffer&) = delete;

    // Enable move operations
    IndexBuffer(IndexBuffer&&) = default;
    IndexBuffer& operator=(IndexBuffer&&) = default;

    void create(VulkanContext* context, const eastl::vector<uint32_t>& indices);
    void create(VulkanContext* context, const eastl::vector<uint16_t>& indices);
    void cleanup();

    vk::Buffer getBuffer() const { return buffer; }
    uint32_t getIndexCount() const { return indexCount; }
    vk::IndexType getIndexType() const { return indexType; }

private:
    VulkanContext* context = nullptr;
    vk::Buffer buffer = nullptr;
    vk::DeviceMemory bufferMemory = nullptr;
    uint32_t indexCount = 0;
    vk::IndexType indexType = vk::IndexType::eUint32;
};

} // namespace violet