#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include "resource/gpu/GPUResource.hpp"
#include "ResourceFactory.hpp"

namespace violet {

class VulkanContext;

class UniformBuffer : public GPUResource {
public:
    UniformBuffer() = default;
    ~UniformBuffer() override { cleanup(); }

    // Delete copy operations
    UniformBuffer(const UniformBuffer&) = delete;
    UniformBuffer& operator=(const UniformBuffer&) = delete;

    // Enable move operations
    UniformBuffer(UniformBuffer&& other) noexcept
        : GPUResource(eastl::move(other))
        , bufferResource(other.bufferResource) {
        other.bufferResource = {};
    }

    UniformBuffer& operator=(UniformBuffer&& other) noexcept {
        if (this != &other) {
            cleanup();
            GPUResource::operator=(eastl::move(other));
            bufferResource = other.bufferResource;
            other.bufferResource = {};
        }
        return *this;
    }

    void create(VulkanContext* context, size_t bufferSize);
    void cleanup() override;
    void update(const void* data, size_t size);

    vk::Buffer getBuffer() const { return bufferResource.buffer; }
    vk::DescriptorBufferInfo getDescriptorInfo() const {
        return vk::DescriptorBufferInfo{bufferResource.buffer, 0, bufferResource.size};
    }

private:
    BufferResource bufferResource;
};

}