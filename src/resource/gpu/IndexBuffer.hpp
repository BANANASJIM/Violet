#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>
#include "resource/gpu/GPUResource.hpp"
#include "ResourceFactory.hpp"

namespace violet {

class VulkanContext;

class IndexBuffer : public GPUResource {
public:
    IndexBuffer() = default;
    ~IndexBuffer() override { cleanup(); }

    // Delete copy operations
    IndexBuffer(const IndexBuffer&) = delete;
    IndexBuffer& operator=(const IndexBuffer&) = delete;

    // Enable move operations
    IndexBuffer(IndexBuffer&& other) noexcept
        : GPUResource(eastl::move(other))
        , bufferResource(other.bufferResource)
        , indexCount(other.indexCount)
        , indexType(other.indexType) {
        other.bufferResource = {};
        other.indexCount = 0;
    }

    IndexBuffer& operator=(IndexBuffer&& other) noexcept {
        if (this != &other) {
            cleanup();
            GPUResource::operator=(eastl::move(other));
            bufferResource = other.bufferResource;
            indexCount = other.indexCount;
            indexType = other.indexType;
            other.bufferResource = {};
            other.indexCount = 0;
        }
        return *this;
    }

    void create(VulkanContext* context, const eastl::vector<uint32_t>& indices);
    void create(VulkanContext* context, const eastl::vector<uint16_t>& indices);
    void cleanup() override;

    vk::Buffer getBuffer() const { return bufferResource.buffer; }
    uint32_t getIndexCount() const { return indexCount; }
    vk::IndexType getIndexType() const { return indexType; }

private:
    BufferResource bufferResource;
    uint32_t indexCount = 0;
    vk::IndexType indexType = vk::IndexType::eUint32;
};

} // namespace violet