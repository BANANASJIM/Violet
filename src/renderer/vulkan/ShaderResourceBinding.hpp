#pragma once

#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include "renderer/vulkan/DescriptorManager.hpp"

namespace violet {

class BufferResource;
class Texture;

// Pure data container for resource bindings
class ShaderResourceBinding {
public:
    ShaderResourceBinding() = default;
    ~ShaderResourceBinding() = default;

    ShaderResourceBinding(const ShaderResourceBinding&) = default;
    ShaderResourceBinding& operator=(const ShaderResourceBinding&) = default;
    ShaderResourceBinding(ShaderResourceBinding&&) = default;
    ShaderResourceBinding& operator=(ShaderResourceBinding&&) = default;

    // ===== Resource Binding API =====

    void bind(uint32_t set, uint32_t binding, const DescriptorResourceHandle& handle);
    void bind(const eastl::string& name, const DescriptorResourceHandle& handle);

    void merge(const ShaderResourceBinding& other);
    void clear();

    // ===== Query API =====

    const eastl::unordered_map<BindingKey, DescriptorResourceHandle>& getResources() const {
        return resources;
    }

    const eastl::unordered_map<eastl::string, DescriptorResourceHandle>& getNameBindings() const {
        return resourcesByName;
    }

    bool hasResource(uint32_t set, uint32_t binding) const;
    const DescriptorResourceHandle* getResource(uint32_t set, uint32_t binding) const;
    size_t size() const { return resources.size(); }
    bool empty() const { return resources.empty(); }

    // ===== Legacy typed binding helpers =====

    void bindBuffer(uint32_t set, uint32_t binding, const BufferResource* buffer,
                   vk::DeviceSize offset = 0, vk::DeviceSize range = VK_WHOLE_SIZE);
    void bindTexture(uint32_t set, uint32_t binding, Texture* texture);
    void bindSampledImage(uint32_t set, uint32_t binding, vk::ImageView imageView,
                          vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal);
    void bindStorageImage(uint32_t set, uint32_t binding, vk::ImageView imageView);
    void bindSampler(uint32_t set, uint32_t binding, vk::Sampler sampler);
    void bindCombinedImageSampler(uint32_t set, uint32_t binding,
                                  vk::ImageView imageView, vk::Sampler sampler,
                                  vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal);

private:
    eastl::unordered_map<BindingKey, DescriptorResourceHandle> resources;
    eastl::unordered_map<eastl::string, DescriptorResourceHandle> resourcesByName;
};

} // namespace violet
