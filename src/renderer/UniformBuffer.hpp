#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

namespace violet {

class VulkanContext;

struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

struct PushConstants {
    alignas(16) glm::vec4 baseColor;
    alignas(4) int hasTexture;
};

class UniformBuffer {
public:
    void create(VulkanContext* context, size_t bufferSize);
    void cleanup();
    void update(const void* data, size_t size);

    vk::Buffer getBuffer() const { return buffer; }
    vk::DescriptorBufferInfo getDescriptorInfo() const {
        return vk::DescriptorBufferInfo{buffer, 0, sizeof(UniformBufferObject)};
    }

private:
    VulkanContext* context = nullptr;
    vk::Buffer buffer = nullptr;
    vk::DeviceMemory bufferMemory = nullptr;
    void* mapped = nullptr;
    size_t bufferSize = 0;
};

}