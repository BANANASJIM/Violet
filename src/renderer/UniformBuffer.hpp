#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include "GPUResource.hpp"
#include "ResourceFactory.hpp"

namespace violet {

class VulkanContext;

struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

struct PushConstants {
    alignas(16) glm::vec4 baseColor;
    alignas(4) float metallic;
    alignas(4) float roughness;
    alignas(4) float normalScale;
    alignas(4) float occlusionStrength;
};

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