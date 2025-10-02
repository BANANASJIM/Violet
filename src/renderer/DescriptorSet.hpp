#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>

namespace violet {

class VulkanContext;
class UniformBuffer;
class Texture;

// NOTE: This enum is DEPRECATED for graphics pipelines
// Graphics pipelines should use DescriptorManager with string-based layout names
// This enum is kept ONLY for ComputePipeline backward compatibility
enum class DescriptorSetType {
    EquirectToCubemap,    // Compute shader: equirectangular to cubemap conversion

    // DEPRECATED - Graphics pipelines should use DescriptorManager instead
    GlobalUniforms,       // Use DescriptorManager::getLayout("Global")
    MaterialTextures,     // Use DescriptorManager::getLayout("PBRMaterial")
    UnlitMaterialTextures, // Use DescriptorManager::getLayout("UnlitMaterial")
    PostProcess,          // Use DescriptorManager::getLayout("PostProcess")
    BindlessTest,         // Use DescriptorManager::getLayout("Bindless")
    None                  // Not needed with DescriptorManager
};

/**
 * DescriptorSet - Lightweight wrapper around pre-allocated descriptor sets
 *
 * NOTE: This class is now a thin wrapper that holds descriptor set handles.
 * All pool/layout creation is managed centrally by DescriptorManager.
 *
 * Usage:
 *   auto sets = descriptorManager.allocateSets("Global", 3);
 *   descriptorSet.init(context, sets);
 *   descriptorSet.updateBuffer(0, buffer);
 */
class DescriptorSet {
public:
    ~DescriptorSet();

    // Modern API: Initialize with pre-allocated descriptor sets from DescriptorManager
    void init(VulkanContext* context, const eastl::vector<vk::DescriptorSet>& sets);

    // Legacy API: COMPUTE SHADER ONLY - Graphics pipelines should use DescriptorManager
    // This method is kept only for ComputePipeline compatibility (e.g., EquirectToCubemap)
    // WARNING: Do NOT use this for graphics pipelines - use DescriptorManager::allocateSets() instead
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