#pragma once

#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include "renderer/vulkan/DescriptorManager.hpp"

namespace violet {

class BufferResource;
class Texture;

/**
 * @brief Pure data container mapping (set, binding) to resource handles
 *
 * This class is a lightweight map that stores descriptor resource bindings.
 * It does NOT manage GPU resources - it only stores the mapping data.
 *
 * Key design principles:
 * - Pure data container (like std::map)
 * - No GPU resource allocation/deallocation
 * - No pipeline or shader dependencies
 * - Safe to copy, move, and use as local variable
 *
 * GPU resource management is handled by DescriptorSetBinding class.
 *
 * Usage:
 *   ShaderResourceBinding resources;
 *   resources.bind(0, 0, DescriptorResourceHandle::fromBuffer(...));
 *   resources.bind(0, 1, DescriptorResourceHandle::fromTexture(...));
 *
 *   // GPU resource allocation happens here:
 *   DescriptorSetBinding gpuBinding(&descriptorMgr, layoutHandle);
 *   gpuBinding.update(resources, frameIndex);
 *   gpuBinding.bind(cmd, pipelineLayout, firstSet, frameIndex);
 *
 * Layered composition example:
 *   ShaderResourceBinding globalResources;  // Camera, lights
 *   globalResources.bind(0, 0, cameraHandle);
 *
 *   ShaderResourceBinding drawResources;
 *   drawResources.merge(globalResources);  // Inherit global bindings
 *   drawResources.bind(1, 0, textureHandle);  // Add draw-specific bindings
 */
class ShaderResourceBinding {
public:
    ShaderResourceBinding() = default;
    ~ShaderResourceBinding() = default;

    // Copyable and movable (pure data)
    ShaderResourceBinding(const ShaderResourceBinding&) = default;
    ShaderResourceBinding& operator=(const ShaderResourceBinding&) = default;
    ShaderResourceBinding(ShaderResourceBinding&&) noexcept = default;
    ShaderResourceBinding& operator=(ShaderResourceBinding&&) noexcept = default;

    // ===== Resource Binding API =====

    /**
     * @brief Bind resource by (set, binding) tuple
     * @param set Descriptor set index
     * @param binding Binding index within the set
     * @param handle Resource handle (buffer, texture, image, sampler)
     */
    void bind(uint32_t set, uint32_t binding, const DescriptorResourceHandle& handle);

    /**
     * @brief Merge another binding into this one
     *
     * Copies all bindings from other. If a binding already exists in this object,
     * it is NOT overwritten (this binding takes precedence).
     *
     * @param other Source binding to merge from
     */
    void merge(const ShaderResourceBinding& other);

    /**
     * @brief Clear all bindings
     */
    void clear();

    // ===== Query API =====

    /**
     * @brief Get all bound resources
     * @return Const reference to resource map
     */
    const eastl::unordered_map<BindingKey, DescriptorResourceHandle>& getResources() const {
        return resources;
    }

    /**
     * @brief Check if a specific binding exists
     * @param set Descriptor set index
     * @param binding Binding index
     * @return true if binding exists, false otherwise
     */
    bool hasResource(uint32_t set, uint32_t binding) const;

    /**
     * @brief Get specific resource handle
     * @param set Descriptor set index
     * @param binding Binding index
     * @return Pointer to resource handle, or nullptr if not found
     */
    const DescriptorResourceHandle* getResource(uint32_t set, uint32_t binding) const;

    /**
     * @brief Get number of bound resources
     * @return Number of bindings
     */
    size_t size() const { return resources.size(); }

    /**
     * @brief Check if binding map is empty
     * @return true if no bindings exist
     */
    bool empty() const { return resources.empty(); }

    // ===== Legacy typed binding helpers (convenience wrappers) =====

    /**
     * @brief Bind buffer resource
     * @param set Descriptor set index
     * @param binding Binding index
     * @param buffer Buffer resource
     * @param offset Offset within buffer
     * @param range Range of buffer data
     */
    void bindBuffer(uint32_t set, uint32_t binding, const BufferResource* buffer,
                   vk::DeviceSize offset = 0, vk::DeviceSize range = VK_WHOLE_SIZE);

    /**
     * @brief Bind texture resource (combined image sampler)
     * @param set Descriptor set index
     * @param binding Binding index
     * @param texture Texture object
     */
    void bindTexture(uint32_t set, uint32_t binding, Texture* texture);

    /**
     * @brief Bind sampled image (separate from sampler)
     * @param set Descriptor set index
     * @param binding Binding index
     * @param imageView Image view
     * @param layout Image layout
     */
    void bindSampledImage(uint32_t set, uint32_t binding, vk::ImageView imageView,
                          vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal);

    /**
     * @brief Bind storage image (RW access)
     * @param set Descriptor set index
     * @param binding Binding index
     * @param imageView Image view
     */
    void bindStorageImage(uint32_t set, uint32_t binding, vk::ImageView imageView);

    /**
     * @brief Bind sampler
     * @param set Descriptor set index
     * @param binding Binding index
     * @param sampler Sampler object
     */
    void bindSampler(uint32_t set, uint32_t binding, vk::Sampler sampler);

    /**
     * @brief Bind combined image sampler (explicit image+sampler)
     * @param set Descriptor set index
     * @param binding Binding index
     * @param imageView Image view
     * @param sampler Sampler
     * @param layout Image layout
     */
    void bindCombinedImageSampler(uint32_t set, uint32_t binding,
                                  vk::ImageView imageView, vk::Sampler sampler,
                                  vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal);

private:
    // The only data member - a pure map
    eastl::unordered_map<BindingKey, DescriptorResourceHandle> resources;
};

} // namespace violet
