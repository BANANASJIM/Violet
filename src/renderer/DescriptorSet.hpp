#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>

namespace violet {

class VulkanContext;
class UniformBuffer;
class Texture;

class DescriptorSet {
public:
    void create(VulkanContext* context, uint32_t maxFramesInFlight);
    void cleanup();

    void updateBuffer(uint32_t frameIndex, UniformBuffer* uniformBuffer);
    void updateTexture(uint32_t frameIndex, Texture* texture);

    vk::DescriptorSetLayout getLayout() const { return descriptorSetLayout; }
    vk::DescriptorSet getDescriptorSet(uint32_t frameIndex) const { return descriptorSets[frameIndex]; }

private:
    VulkanContext* context = nullptr;
    vk::DescriptorPool descriptorPool;
    vk::DescriptorSetLayout descriptorSetLayout;
    eastl::vector<vk::DescriptorSet> descriptorSets;
};

}