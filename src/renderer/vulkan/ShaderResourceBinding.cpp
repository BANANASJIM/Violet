#include "ShaderResourceBinding.hpp"
#include "resource/gpu/ResourceFactory.hpp"
#include "resource/Texture.hpp"
#include "core/Log.hpp"

namespace violet {

void ShaderResourceBinding::bind(uint32_t set, uint32_t binding, const DescriptorResourceHandle& handle) {
    resources[{set, binding}] = handle;
}

void ShaderResourceBinding::merge(const ShaderResourceBinding& other) {
    // Copy all bindings from other (don't overwrite existing - this binding takes precedence)
    for (const auto& [key, handle] : other.resources) {
        if (resources.find(key) == resources.end()) {
            resources[key] = handle;
        }
    }
}

void ShaderResourceBinding::clear() {
    resources.clear();
}

bool ShaderResourceBinding::hasResource(uint32_t set, uint32_t binding) const {
    return resources.find({set, binding}) != resources.end();
}

const DescriptorResourceHandle* ShaderResourceBinding::getResource(uint32_t set, uint32_t binding) const {
    auto it = resources.find({set, binding});
    return it != resources.end() ? &it->second : nullptr;
}

// ===== Legacy typed binding helpers =====

void ShaderResourceBinding::bindBuffer(uint32_t set, uint32_t binding,
                                       const BufferResource* buffer,
                                       vk::DeviceSize offset, vk::DeviceSize range) {
    if (!buffer) {
        violet::Log::warn("ShaderResourceBinding",
            "Binding null buffer to set {} binding {}", set, binding);
        return;
    }

    bind(set, binding, DescriptorResourceHandle::fromBuffer(buffer->buffer, offset, range));
}

void ShaderResourceBinding::bindTexture(uint32_t set, uint32_t binding, Texture* texture) {
    if (!texture) {
        violet::Log::warn("ShaderResourceBinding",
            "Binding null texture to set {} binding {}", set, binding);
        return;
    }

    bind(set, binding, DescriptorResourceHandle::fromTexture(texture));
}

void ShaderResourceBinding::bindSampledImage(uint32_t set, uint32_t binding,
                                             vk::ImageView imageView, vk::ImageLayout layout) {
    if (!imageView) {
        violet::Log::warn("ShaderResourceBinding",
            "Binding null image view to set {} binding {}", set, binding);
        return;
    }

    bind(set, binding, DescriptorResourceHandle::fromSampledImage(imageView, layout));
}

void ShaderResourceBinding::bindStorageImage(uint32_t set, uint32_t binding, vk::ImageView imageView) {
    if (!imageView) {
        violet::Log::warn("ShaderResourceBinding",
            "Binding null image view to set {} binding {}", set, binding);
        return;
    }

    bind(set, binding, DescriptorResourceHandle::fromStorageImage(imageView));
}

void ShaderResourceBinding::bindSampler(uint32_t set, uint32_t binding, vk::Sampler sampler) {
    if (!sampler) {
        violet::Log::warn("ShaderResourceBinding",
            "Binding null sampler to set {} binding {}", set, binding);
        return;
    }

    bind(set, binding, DescriptorResourceHandle::fromSampler(sampler));
}

void ShaderResourceBinding::bindCombinedImageSampler(uint32_t set, uint32_t binding,
                                                     vk::ImageView imageView, vk::Sampler sampler,
                                                     vk::ImageLayout layout) {
    if (!imageView || !sampler) {
        violet::Log::warn("ShaderResourceBinding",
            "Binding null image/sampler to set {} binding {}", set, binding);
        return;
    }

    bind(set, binding, DescriptorResourceHandle::fromCombinedImageSampler(imageView, sampler, layout));
}

} // namespace violet
