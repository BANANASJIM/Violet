#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>

namespace violet {

class VulkanContext;
class UniformBuffer;
class Texture;

enum class DescriptorSetType {
    GlobalUniforms,       // 全局uniform buffer (相机等)
    MaterialTextures,     // PBR材质纹理采样器
    UnlitMaterialTextures, // Unlit材质纹理采样器 (简化版)
    EquirectToCubemap,    // 计算着色器: equirectangular to cubemap
    None                  // 不创建任何descriptor set (仅使用全局set)
};

class DescriptorSet {
public:
    ~DescriptorSet();
    void create(VulkanContext* context, uint32_t maxFramesInFlight);
    void create(VulkanContext* context, uint32_t maxFramesInFlight, DescriptorSetType type);
    void cleanup();

    void updateBuffer(uint32_t frameIndex, UniformBuffer* uniformBuffer);
    void updateTexture(uint32_t frameIndex, Texture* texture);
    void updateUniformBuffer(uint32_t frameIndex, UniformBuffer* uniformBuffer, uint32_t binding);
    void updateTexture(uint32_t frameIndex, Texture* texture, uint32_t binding);
    void updateStorageImage(uint32_t frameIndex, Texture* texture, uint32_t binding);

    vk::DescriptorSetLayout getLayout() const { return descriptorSetLayout; }
    vk::DescriptorSet getDescriptorSet(uint32_t frameIndex) const { return descriptorSets[frameIndex]; }

private:
    VulkanContext* context = nullptr;
    vk::DescriptorPool descriptorPool;
    vk::DescriptorSetLayout descriptorSetLayout;
    eastl::vector<vk::DescriptorSet> descriptorSets;
};

}