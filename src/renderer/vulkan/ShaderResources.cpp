#include "ShaderResources.hpp"
#include "VulkanContext.hpp"
#include "resource/shader/Shader.hpp"
#include "resource/Texture.hpp"
#include "core/Log.hpp"
#include <EASTL/sort.h>

namespace violet {

// ===== ElementProxy Implementation =====

ElementProxy::ElementProxy(ShaderResources* parent, const ReflectedResource* resourceInfo,
                           size_t elementIndex, void* bufferData, uint32_t elementStride)
    : parent(parent), resourceInfo(resourceInfo), elementIndex(elementIndex),
      bufferData(bufferData), elementStride(elementStride) {}

FieldProxy ElementProxy::operator[](const eastl::string& fieldName) {
    if (!resourceInfo || !bufferData || !parent) {
        Log::error("ShaderResources", "Invalid element proxy");
        return FieldProxy(nullptr, 0, 0, "invalid");
    }

    const ReflectedBuffer* bufferLayout = parent->reflection.getBufferLayout(resourceInfo->bufferLayoutIndex);
    if (!bufferLayout) {
        Log::error("ShaderResources", "Resource '{}' has no buffer layout",
                   resourceInfo->name.c_str());
        return FieldProxy(nullptr, 0, 0, "invalid");
    }

    const ReflectedField* field = nullptr;
    for (const auto& f : bufferLayout->fields) {
        if (f.name == fieldName) {
            field = &f;
            break;
        }
    }

    if (!field) {
        Log::error("ShaderResources", "Field '{}' not found in buffer '{}' element",
                   fieldName.c_str(), resourceInfo->name.c_str());
        return FieldProxy(nullptr, 0, 0, "invalid");
    }

    uint32_t totalOffset = elementIndex * elementStride + field->offset;
    return FieldProxy(bufferData, totalOffset, field->size, fieldName);
}

// ===== ResourceProxy Implementation =====

ResourceProxy::ResourceProxy(ShaderResources* parent, const ReflectedResource* resourceInfo)
    : parent(parent), resourceInfo(resourceInfo) {}

FieldProxy ResourceProxy::operator[](const eastl::string& fieldName) {
    if (!resourceInfo || !parent) {
        Log::error("ShaderResources", "Invalid resource proxy");
        return FieldProxy(nullptr, 0, 0, "invalid");
    }

    if (resourceInfo->type != vk::DescriptorType::eUniformBuffer &&
        resourceInfo->type != vk::DescriptorType::eStorageBuffer) {
        Log::error("ShaderResources", "Resource '{}' is not a buffer (cannot access fields)",
                   resourceInfo->name.c_str());
        return FieldProxy(nullptr, 0, 0, "invalid");
    }

    const ReflectedBuffer* bufferLayout = parent->reflection.getBufferLayout(resourceInfo->bufferLayoutIndex);
    if (!bufferLayout) {
        Log::error("ShaderResources", "Resource '{}' has no buffer layout",
                   resourceInfo->name.c_str());
        return FieldProxy(nullptr, 0, 0, "invalid");
    }

    const ReflectedField* field = nullptr;
    for (const auto& f : bufferLayout->fields) {
        if (f.name == fieldName) {
            field = &f;
            break;
        }
    }

    if (!field) {
        Log::error("ShaderResources", "Field '{}' not found in buffer '{}'",
                   fieldName.c_str(), resourceInfo->name.c_str());
        return FieldProxy(nullptr, 0, 0, "invalid");
    }

    auto setIt = parent->sets.find(resourceInfo->set);
    if (setIt == parent->sets.end()) {
        Log::error("ShaderResources", "Set {} not found", resourceInfo->set);
        return FieldProxy(nullptr, 0, 0, "invalid");
    }

    const auto& setData = setIt->second;
    void* basePtr = nullptr;
    uint32_t alignedSize = 0;

    // Try to get buffer from per-binding map first
    auto bindingIt = setData.buffersByBinding.find(resourceInfo->binding);
    if (bindingIt != setData.buffersByBinding.end()) {
        basePtr = bindingIt->second.mappedData;
        alignedSize = bindingIt->second.alignedSize;
    } else if (setData.mappedData) {
        // Fallback to legacy single buffer
        basePtr = setData.mappedData;
        alignedSize = setData.alignedSize;
    }

    if (!basePtr) {
        Log::error("ShaderResources", "Buffer for set {} binding {} is not mapped",
                   resourceInfo->set, resourceInfo->binding);
        return FieldProxy(nullptr, 0, 0, "invalid");
    }

    if (setData.frequency == UpdateFrequency::PerFrame) {
        basePtr = static_cast<char*>(basePtr) + (parent->getCurrentFrame() * alignedSize);
    }

    return FieldProxy(basePtr, field->offset, field->size, fieldName);
}

ElementProxy ResourceProxy::operator[](size_t elementIndex) {
    if (!resourceInfo || !parent) {
        Log::error("ShaderResources", "Invalid resource proxy");
        return ElementProxy(nullptr, nullptr, 0, nullptr, 0);
    }

    if (resourceInfo->type != vk::DescriptorType::eStorageBuffer) {
        Log::error("ShaderResources", "Resource '{}' is not a StorageBuffer (cannot access array elements)",
                   resourceInfo->name.c_str());
        return ElementProxy(nullptr, nullptr, 0, nullptr, 0);
    }

    const ReflectedBuffer* bufferLayout = parent->reflection.getBufferLayout(resourceInfo->bufferLayoutIndex);
    if (!bufferLayout) {
        Log::error("ShaderResources", "Resource '{}' has no buffer layout",
                   resourceInfo->name.c_str());
        return ElementProxy(nullptr, nullptr, 0, nullptr, 0);
    }

    auto setIt = parent->sets.find(resourceInfo->set);
    if (setIt == parent->sets.end()) {
        Log::error("ShaderResources", "Set {} not found", resourceInfo->set);
        return ElementProxy(nullptr, nullptr, 0, nullptr, 0);
    }

    const auto& setData = setIt->second;
    void* basePtr = nullptr;
    uint32_t alignedSize = 0;

    // Try to get buffer from per-binding map first
    auto bindingIt = setData.buffersByBinding.find(resourceInfo->binding);
    if (bindingIt != setData.buffersByBinding.end()) {
        basePtr = bindingIt->second.mappedData;
        alignedSize = bindingIt->second.alignedSize;
    } else if (setData.mappedData) {
        // Fallback to legacy single buffer
        basePtr = setData.mappedData;
        alignedSize = setData.alignedSize;
    }

    if (!basePtr) {
        Log::error("ShaderResources", "Buffer for set {} binding {} is not mapped",
                   resourceInfo->set, resourceInfo->binding);
        return ElementProxy(nullptr, nullptr, 0, nullptr, 0);
    }

    uint32_t elementStride = bufferLayout->totalSize;

    if (setData.frequency == UpdateFrequency::PerFrame) {
        basePtr = static_cast<char*>(basePtr) + (parent->getCurrentFrame() * alignedSize);
    }

    return ElementProxy(parent, resourceInfo, elementIndex, basePtr, elementStride);
}

ResourceProxy& ResourceProxy::operator=(Texture* texture) {
    if (!resourceInfo || resourceInfo->type != vk::DescriptorType::eCombinedImageSampler) {
        Log::error("ShaderResources", "Resource '{}' is not a CombinedImageSampler",
                   resourceInfo ? resourceInfo->name.c_str() : "null");
        return *this;
    }

    auto setIt = parent->sets.find(resourceInfo->set);
    if (setIt == parent->sets.end()) {
        Log::error("ShaderResources", "Set {} not found", resourceInfo->set);
        return *this;
    }

    // Use DescriptorManager for binding
    if (parent->descriptorManager) {
        parent->descriptorManager->bindTexture(setIt->second.descriptorSet, resourceInfo->binding, texture);
    } else {
        Log::error("ShaderResources", "DescriptorManager is null, cannot bind texture");
    }

    return *this;
}

ResourceProxy& ResourceProxy::operator=(const StorageBufferBinding& binding) {
    if (!resourceInfo || resourceInfo->type != vk::DescriptorType::eStorageBuffer) {
        Log::error("ShaderResources", "Resource '{}' is not a StorageBuffer",
                   resourceInfo ? resourceInfo->name.c_str() : "null");
        return *this;
    }

    auto setIt = parent->sets.find(resourceInfo->set);
    if (setIt == parent->sets.end()) {
        return *this;
    }

    // Use DescriptorManager for binding
    if (parent->descriptorManager) {
        BufferResource bufRes{binding.buffer, nullptr};
        parent->descriptorManager->bindBuffer(setIt->second.descriptorSet, resourceInfo->binding,
                                             bufRes, vk::DescriptorType::eStorageBuffer,
                                             binding.offset, binding.range);
    } else {
        Log::error("ShaderResources", "DescriptorManager is null, cannot bind storage buffer");
    }

    return *this;
}

ResourceProxy& ResourceProxy::operator=(vk::ImageView imageView) {
    if (!resourceInfo || resourceInfo->type != vk::DescriptorType::eStorageImage) {
        Log::error("ShaderResources", "Resource '{}' is not a StorageImage",
                   resourceInfo ? resourceInfo->name.c_str() : "null");
        return *this;
    }

    auto setIt = parent->sets.find(resourceInfo->set);
    if (setIt == parent->sets.end()) {
        return *this;
    }

    // Use DescriptorManager for binding
    if (parent->descriptorManager) {
        parent->descriptorManager->bindStorageImage(setIt->second.descriptorSet, resourceInfo->binding, imageView);
    } else {
        Log::error("ShaderResources", "DescriptorManager is null, cannot bind storage image");
    }

    return *this;
}

ResourceProxy& ResourceProxy::operator=(const BufferResource& buffer) {
    if (!resourceInfo || resourceInfo->type != vk::DescriptorType::eUniformBuffer) {
        Log::error("ShaderResources", "Resource '{}' is not a UniformBuffer",
                   resourceInfo ? resourceInfo->name.c_str() : "null");
        return *this;
    }

    auto setIt = parent->sets.find(resourceInfo->set);
    if (setIt == parent->sets.end()) {
        return *this;
    }

    // Use DescriptorManager for binding
    if (parent->descriptorManager) {
        parent->descriptorManager->bindBuffer(setIt->second.descriptorSet, resourceInfo->binding,
                                             buffer, vk::DescriptorType::eUniformBuffer);
    } else {
        Log::error("ShaderResources", "DescriptorManager is null, cannot bind uniform buffer");
    }

    return *this;
}

ResourceProxy& ResourceProxy::operator=(vk::Sampler sampler) {
    if (!resourceInfo || resourceInfo->type != vk::DescriptorType::eSampler) {
        Log::error("ShaderResources", "Resource '{}' is not a Sampler",
                   resourceInfo ? resourceInfo->name.c_str() : "null");
        return *this;
    }

    auto setIt = parent->sets.find(resourceInfo->set);
    if (setIt == parent->sets.end()) {
        Log::error("ShaderResources", "Set {} not found", resourceInfo->set);
        return *this;
    }

    // Use DescriptorManager for binding
    if (parent->descriptorManager) {
        parent->descriptorManager->bindSampler(setIt->second.descriptorSet, resourceInfo->binding, sampler);
    } else {
        Log::error("ShaderResources", "DescriptorManager is null, cannot bind sampler");
    }

    return *this;
}

vk::DescriptorType ResourceProxy::getType() const {
    return resourceInfo ? resourceInfo->type : vk::DescriptorType::eUniformBuffer;
}

const eastl::string& ResourceProxy::getName() const {
    static eastl::string empty;
    return resourceInfo ? resourceInfo->name : empty;
}

// ===== ShaderResources Implementation =====

ShaderResources::ShaderResources(
    eastl::string instanceName,
    eastl::shared_ptr<Shader> shader,
    ShaderReflection reflection,
    VulkanContext* context,
    uint32_t maxFrames,
    DescriptorManager* descriptorMgr)
    : instanceName(eastl::move(instanceName))
    , shader(eastl::move(shader))
    , reflection(eastl::move(reflection))
    , context(context)
    , descriptorManager(descriptorMgr)
    , maxFrames(maxFrames) {
}

ShaderResources::~ShaderResources() {
    for (auto& [setIndex, setData] : sets) {
        // Cleanup per-binding buffers
        for (auto& [binding, bufferData] : setData.buffersByBinding) {
            if (bufferData.buffer.buffer) {
                ResourceFactory::destroyBuffer(context, bufferData.buffer);
            }
        }

        // Cleanup legacy single buffer
        if (setData.hasBuffer && setData.buffer.buffer) {
            ResourceFactory::destroyBuffer(context, setData.buffer);
        }
    }
}

ResourceProxy ShaderResources::operator[](const eastl::string& resourceName) {
    const ReflectedResource* resource = reflection.findResource(resourceName);
    if (!resource) {
        Log::error("ShaderResources", "Resource '{}' not found in shader '{}'",
                   resourceName.c_str(), shader ? shader->getName().c_str() : "null");
        return ResourceProxy(this, nullptr);
    }

    return ResourceProxy(this, resource);
}

void ShaderResources::updateResources(uint32_t setIndex,
                                      const eastl::unordered_map<eastl::string, DescriptorResourceHandle>& resources) {
    auto setIt = sets.find(setIndex);
    if (setIt == sets.end()) {
        Log::error("ShaderResources", "Set {} not found in ShaderResources '{}'",
                  setIndex, instanceName.c_str());
        return;
    }

    if (!descriptorManager) {
        Log::error("ShaderResources", "DescriptorManager is null, cannot batch update descriptors");
        return;
    }

    // Delegate to DescriptorManager's reflection-driven batch update
    descriptorManager->updateSet(setIt->second.descriptorSet, reflection, resources);

    Log::debug("ShaderResources", "Batch updated {} resource(s) in set {} for '{}'",
              resources.size(), setIndex, instanceName.c_str());
}

vk::DescriptorSet ShaderResources::getSet(uint32_t setIndex) const {
    auto it = sets.find(setIndex);
    if (it == sets.end()) {
        return nullptr;
    }
    return it->second.descriptorSet;
}

uint32_t ShaderResources::getDynamicOffset(uint32_t setIndex, uint32_t frameIndex) const {
    auto it = sets.find(setIndex);
    if (it == sets.end()) {
        Log::error("ShaderResources", "getDynamicOffset: set {} not found in '{}'", setIndex, instanceName.c_str());
        return 0;
    }

    if (it->second.frequency == UpdateFrequency::PerFrame) {
        // Use currentFrame member set by setCurrentFrame(), not the parameter
        // The frameIndex parameter is deprecated but kept for API compatibility
        uint32_t offset = currentFrame * it->second.alignedSize;
        return offset;
    }

    return 0;
}

eastl::vector<uint32_t> ShaderResources::getDynamicOffsetsForSet(uint32_t setIndex) const {
    eastl::vector<uint32_t> offsets;

    auto setIt = sets.find(setIndex);
    if (setIt == sets.end()) {
        Log::error("ShaderResources", "getDynamicOffsetsForSet: set {} not found in '{}'", setIndex, instanceName.c_str());
        return offsets;
    }

    const auto& setData = setIt->second;

    // Only PerFrame sets have dynamic offsets
    if (setData.frequency != UpdateFrequency::PerFrame) {
        return offsets;
    }

    // Check if using per-binding buffers or legacy single buffer
    if (!setData.buffersByBinding.empty()) {
        // Per-binding buffer path (Set 0 with camera/lights/shadows)
        // Collect all buffer bindings in sorted order (by binding number)
        eastl::vector<eastl::pair<uint32_t, uint32_t>> bindingOffsets;  // (binding, alignedSize)
        for (const auto& [binding, bufferData] : setData.buffersByBinding) {
            bindingOffsets.push_back({binding, bufferData.alignedSize});
        }

        // Sort by binding number (Vulkan requires offsets in binding order)
        eastl::sort(bindingOffsets.begin(), bindingOffsets.end(),
                   [](const auto& a, const auto& b) { return a.first < b.first; });

        // Calculate offset for each binding: currentFrame * alignedSize
        offsets.reserve(bindingOffsets.size());
        for (const auto& [binding, alignedSize] : bindingOffsets) {
            uint32_t offset = currentFrame * alignedSize;
            offsets.push_back(offset);
        }
    } else if (setData.hasBuffer) {
        // Legacy single buffer path (Set 2 with materials SSBO)
        uint32_t offset = currentFrame * setData.alignedSize;
        offsets.push_back(offset);
    }

    return offsets;
}

void ShaderResources::bind(vk::CommandBuffer cmd, vk::PipelineLayout layout,
                           vk::PipelineBindPoint bindPoint, uint32_t frameIndex) {
    if (sets.empty()) {
        return;
    }

    for (auto& [setIndex, setData] : sets) {
        eastl::vector<uint32_t> dynamicOffsets;

        if (setData.frequency == UpdateFrequency::PerFrame && setData.hasBuffer) {
            dynamicOffsets.push_back(frameIndex * setData.alignedSize);
        }

        cmd.bindDescriptorSets(
            bindPoint,
            layout,
            setIndex,
            {setData.descriptorSet},
            dynamicOffsets
        );
    }
}

bool ShaderResources::hasResource(const eastl::string& name) const {
    return reflection.findResource(name) != nullptr;
}

const ReflectedResource* ShaderResources::getResourceInfo(const eastl::string& name) const {
    return reflection.findResource(name);
}

} // namespace violet