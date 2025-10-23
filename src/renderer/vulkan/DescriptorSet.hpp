#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>

namespace violet {

class VulkanContext;
class UniformBuffer;
class Texture;

// NOTE: This enum is ONLY for compute shaders
// Graphics pipelines should use DescriptorManager with string-based layout names
enum class DescriptorSetType {
    EquirectToCubemap    // Compute shader: equirectangular to cubemap conversion
};

/**
 * DescriptorSet - Lightweight wrapper around pre-allocated descriptor sets
 *
 * DEPRECATED: Use DescriptorManager::createUniform() with UniformHandle instead
 * - UniformHandle manages descriptor sets automatically with dynamic offset
 * - No need to manually allocate sets or update descriptors
 * - Reflection-based field updates via uniform["fieldName"] = value
 *
 * Old usage (DEPRECATED):
 *   auto sets = descriptorManager.allocateSets("Global", 3);
 *   descriptorSet.init(context, sets);
 *   descriptorSet.updateBuffer(0, buffer);
 *
 * New usage:
 *   UniformHandle uniform = descriptorManager.createUniform("Global", layoutHandle, UpdateFrequency::PerFrame);
 *   uniform["view"] = viewMatrix;
 *   vk::DescriptorSet set = uniform.getSet();
 *   uint32_t offset = uniform.getDynamicOffset();
 */
class [[deprecated("Use DescriptorManager::createUniform() with UniformHandle instead of manual DescriptorSet management")]] DescriptorSet {
public:
    ~DescriptorSet();

    // Modern API: Initialize with pre-allocated descriptor sets from DescriptorManager
    void init(VulkanContext* context, const eastl::vector<vk::DescriptorSet>& sets);

    // Compute shader API: Only for compute pipelines (e.g., EquirectToCubemap)
    // Graphics pipelines must use DescriptorManager::allocateSets() instead
    void create(VulkanContext* context, uint32_t maxFramesInFlight);
    void create(VulkanContext* context, uint32_t maxFramesInFlight, DescriptorSetType type);

    void cleanup();

    // Update methods (compatible with both old and new API)
    void updateBuffer(uint32_t frameIndex, UniformBuffer* uniformBuffer);
    void updateTexture(uint32_t frameIndex, Texture* texture);
    void updateUniformBuffer(uint32_t frameIndex, UniformBuffer* uniformBuffer, uint32_t binding);
    void updateTexture(uint32_t frameIndex, Texture* texture, uint32_t binding);
    void updateStorageImage(uint32_t frameIndex, Texture* texture, uint32_t binding);

    // Legacy method - returns nullptr with new init() API
    vk::DescriptorSetLayout getLayout() const { return descriptorSetLayout; }
    vk::DescriptorSet getDescriptorSet(uint32_t frameIndex) const { return descriptorSets[frameIndex]; }

private:
    VulkanContext* context = nullptr;

    // Legacy members (only used with old create() API)
    vk::DescriptorPool descriptorPool;
    vk::DescriptorSetLayout descriptorSetLayout;

    // Descriptor set handles (used with both old and new API)
    eastl::vector<vk::DescriptorSet> descriptorSets;
};

}