#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>
#include "renderer/vulkan/DescriptorManager.hpp"

namespace violet {

class ShaderResourceBinding;

/**
 * @brief RAII wrapper for managing GPU descriptor sets
 *
 * This class owns and manages the lifecycle of descriptor sets allocated from DescriptorManager.
 * It automatically allocates descriptor sets on construction and frees them on destruction.
 *
 * Responsibilities:
 * - Allocate descriptor set group from DescriptorManager
 * - Free descriptor set group on destruction
 * - Update descriptor sets from ShaderResourceBinding data
 * - Bind descriptor sets to command buffers
 *
 * Lifecycle:
 * - Non-copyable (descriptor sets are GPU resources)
 * - Movable (ownership can be transferred)
 * - Typically used as:
 *   - Local variable for temporary bindings (single-time commands)
 *   - Member variable (unique_ptr) for long-lived bindings (materials, passes)
 *
 * Example usage (temporary):
 *   ShaderResourceBinding resources;
 *   resources.bind(0, 0, handle1);
 *   resources.bind(0, 1, handle2);
 *
 *   DescriptorSetBinding gpuBinding(&descriptorMgr, layoutHandle);
 *   gpuBinding.update(resources, frameIndex);
 *   gpuBinding.bind(cmd, pipelineLayout, firstSet, frameIndex);
 *   // gpuBinding destructs, freeing GPU resources
 *
 * Example usage (long-lived):
 *   class Material {
 *       ShaderResourceBinding textureMap;
 *       eastl::unique_ptr<DescriptorSetBinding> gpuBinding;
 *
 *       void init() {
 *           gpuBinding = eastl::make_unique<DescriptorSetBinding>(&mgr, layout);
 *       }
 *   };
 */
class DescriptorSetBinding {
public:
    /**
     * @brief Construct and allocate descriptor set group
     * @param manager Descriptor manager (must outlive this object)
     * @param layoutHandle Layout handle for descriptor set layout
     */
    DescriptorSetBinding(DescriptorManager* manager, LayoutHandle layoutHandle);

    /**
     * @brief Destructor - automatically frees descriptor set group
     */
    ~DescriptorSetBinding();

    // Non-copyable (GPU resources cannot be arbitrarily copied)
    DescriptorSetBinding(const DescriptorSetBinding&) = delete;
    DescriptorSetBinding& operator=(const DescriptorSetBinding&) = delete;

    // Movable (ownership transfer)
    DescriptorSetBinding(DescriptorSetBinding&& other) noexcept;
    DescriptorSetBinding& operator=(DescriptorSetBinding&& other) noexcept;

    /**
     * @brief Update descriptor sets from ShaderResourceBinding data
     *
     * Reads the resource map from ShaderResourceBinding and updates the corresponding
     * descriptor sets. This should be called whenever the resources change.
     *
     * @param resources Pure data container with resource bindings
     * @param frameIndex Frame index for per-frame resources (0-2 for triple buffering)
     */
    void update(const ShaderResourceBinding& resources, uint32_t frameIndex);

    /**
     * @brief Bind descriptor sets to command buffer
     *
     * @param cmd Command buffer
     * @param pipelineLayout Pipeline layout (must match the layout used for descriptor sets)
     * @param firstSet First set index to bind to (typically 0, 1, or 2 depending on the set)
     * @param frameIndex Frame index for per-frame resources
     * @param bindPoint Pipeline bind point (eGraphics or eCompute)
     * @param dynamicOffsets Dynamic offsets for dynamic uniform/storage buffers
     */
    void bind(vk::CommandBuffer cmd,
             vk::PipelineLayout pipelineLayout,
             uint32_t firstSet,
             uint32_t frameIndex,
             vk::PipelineBindPoint bindPoint = vk::PipelineBindPoint::eGraphics,
             const eastl::vector<uint32_t>& dynamicOffsets = {});

    /**
     * @brief Check if descriptor set binding is valid
     * @return true if setGroupHandle is allocated, false otherwise
     */
    bool isValid() const { return setGroupHandle != 0; }

    /**
     * @brief Get the underlying set group handle (for debugging)
     * @return SetGroupHandle
     */
    SetGroupHandle getHandle() const { return setGroupHandle; }

private:
    /**
     * @brief Release resources (used by destructor and move assignment)
     */
    void cleanup();

    DescriptorManager* manager = nullptr;
    SetGroupHandle setGroupHandle = 0;
    LayoutHandle layoutHandle = 0;
};

} // namespace violet
