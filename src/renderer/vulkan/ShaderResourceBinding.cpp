#include "ShaderResourceBinding.hpp"
#include "renderer/vulkan/GraphicsPipeline.hpp"
#include "renderer/vulkan/ComputePipeline.hpp"
#include "resource/shader/Shader.hpp"
#include "resource/gpu/ResourceFactory.hpp"
#include "resource/Texture.hpp"
#include "core/Log.hpp"
#include <EASTL/hash_set.h>

namespace violet {

vk::PipelineLayout ShaderResourceBinding::getPipelineLayout() const {
    if (graphicsPipeline) {
        return graphicsPipeline->getPipelineLayout();
    } else if (computePipeline) {
        return computePipeline->getPipelineLayout();
    }
    return nullptr;
}

const ShaderReflection* ShaderResourceBinding::getShaderReflection() const {
    auto shaderPtr = shader.lock();
    if (shaderPtr) {
        return shaderPtr->getShaderReflection();
    }
    return nullptr;
}

void ShaderResourceBinding::init(GraphicsPipeline* pipeline, uint32_t setIdx) {
    if (!pipeline) {
        violet::Log::error("ShaderResourceBinding", "Cannot init with null GraphicsPipeline");
        return;
    }

    graphicsPipeline = pipeline;
    computePipeline = nullptr;
    setIndex = setIdx;
    setGroupHandle = 0;  // Will be allocated by DescriptorManager on first use
    resources.clear();
    dirty = true;

    // Cache shader (prefer vertex shader, fallback to fragment shader)
    shader = pipeline->getVertexShader();
    if (shader.expired()) {
        shader = pipeline->getFragmentShader();
    }

    // Cache layout handle from shader
    auto shaderPtr = shader.lock();
    if (shaderPtr) {
        const auto& layoutHandles = shaderPtr->getDescriptorLayoutHandles();
        if (setIdx < layoutHandles.size()) {
            layoutHandle = layoutHandles[setIdx];
        } else {
            violet::Log::warn("ShaderResourceBinding", "Set index {} out of range (shader has {} sets)",
                            setIdx, layoutHandles.size());
        }
    }

    violet::Log::debug("ShaderResourceBinding", "Initialized with GraphicsPipeline, set {} (layout handle: {})",
                      setIdx, layoutHandle);
}

void ShaderResourceBinding::init(ComputePipeline* pipeline, uint32_t setIdx) {
    if (!pipeline) {
        violet::Log::error("ShaderResourceBinding", "Cannot init with null ComputePipeline");
        return;
    }

    graphicsPipeline = nullptr;
    computePipeline = pipeline;
    setIndex = setIdx;
    setGroupHandle = 0;  // Will be allocated by DescriptorManager on first use
    resources.clear();
    dirty = true;

    // Cache shader
    shader = pipeline->getShader();

    // Cache layout handle from shader
    auto shaderPtr = shader.lock();
    if (shaderPtr) {
        const auto& layoutHandles = shaderPtr->getDescriptorLayoutHandles();
        if (setIdx < layoutHandles.size()) {
            layoutHandle = layoutHandles[setIdx];
        } else {
            violet::Log::warn("ShaderResourceBinding", "Set index {} out of range (shader has {} sets)",
                            setIdx, layoutHandles.size());
        }
    }

    violet::Log::debug("ShaderResourceBinding", "Initialized with ComputePipeline, set {} (layout handle: {})",
                      setIdx, layoutHandle);
}

void ShaderResourceBinding::bindBuffer(const eastl::string& name, const BufferResource* buffer,
                                       vk::DeviceSize offset, vk::DeviceSize range) {
    if (!buffer) {
        violet::Log::warn("ShaderResourceBinding", "Binding null buffer to '{}'", name.c_str());
        return;
    }

    resources[name] = DescriptorResourceHandle::fromBuffer(buffer->buffer, offset, range);
    dirty = true;
}

void ShaderResourceBinding::bindTexture(const eastl::string& name, Texture* texture) {
    if (!texture) {
        violet::Log::warn("ShaderResourceBinding", "Binding null texture to '{}'", name.c_str());
        return;
    }

    resources[name] = DescriptorResourceHandle::fromTexture(texture);
    dirty = true;
}

void ShaderResourceBinding::bindSampledImage(const eastl::string& name, vk::ImageView imageView, vk::ImageLayout layout) {
    if (!imageView) {
        violet::Log::warn("ShaderResourceBinding", "Binding null image view to '{}'", name.c_str());
        return;
    }

    resources[name] = DescriptorResourceHandle::fromSampledImage(imageView, layout);
    dirty = true;
}

void ShaderResourceBinding::bindStorageImage(const eastl::string& name, vk::ImageView imageView) {
    if (!imageView) {
        violet::Log::warn("ShaderResourceBinding", "Binding null image view to '{}'", name.c_str());
        return;
    }

    resources[name] = DescriptorResourceHandle::fromStorageImage(imageView);
    dirty = true;
}

void ShaderResourceBinding::bindSampler(const eastl::string& name, vk::Sampler sampler) {
    if (!sampler) {
        violet::Log::warn("ShaderResourceBinding", "Binding null sampler to '{}'", name.c_str());
        return;
    }

    resources[name] = DescriptorResourceHandle::fromSampler(sampler);
    dirty = true;
}

void ShaderResourceBinding::bindCombinedImageSampler(const eastl::string& name, vk::ImageView imageView,
                                                     vk::Sampler sampler, vk::ImageLayout layout) {
    if (!imageView || !sampler) {
        violet::Log::warn("ShaderResourceBinding", "Binding null image/sampler to '{}'", name.c_str());
        return;
    }

    resources[name] = DescriptorResourceHandle::fromCombinedImageSampler(imageView, sampler, layout);
    dirty = true;
}

uint64_t ShaderResourceBinding::generateCacheKey() const {
    uint64_t hash = 0;

    // Hash based on resource names + types (not actual pointers)
    // This allows reusing bindings with same structure but different resource instances
    for (const auto& [name, resource] : resources) {
        // Hash the name
        uint64_t nameHash = eastl::hash<eastl::string>{}(name);
        hash ^= nameHash + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);

        // Hash the resource type
        uint64_t typeHash = static_cast<uint64_t>(resource.type);
        hash ^= typeHash + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    }

    return hash;
}

} // namespace violet