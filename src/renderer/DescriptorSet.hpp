#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>

namespace violet {

class VulkanContext;
class UniformBuffer;
class Texture;

enum class DescriptorSetType {
    GlobalUniforms,    // 全局uniform buffer (相机等)
    MaterialTextures   // 材质纹理采样器
};

class DescriptorSet {
public:
    ~DescriptorSet();
    void create(VulkanContext* context, uint32_t maxFramesInFlight);
    void create(VulkanContext* context, uint32_t maxFramesInFlight, DescriptorSetType type);
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