#include "ShaderResources.hpp"
#include "VulkanContext.hpp"
#include "resource/shader/Shader.hpp"
#include "resource/Texture.hpp"
#include "core/Log.hpp"

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
    if (setIt == parent->sets.end() || !setIt->second.mappedData) {
        Log::error("ShaderResources", "Buffer for set {} is not mapped", resourceInfo->set);
        return FieldProxy(nullptr, 0, 0, "invalid");
    }

    void* basePtr = setIt->second.mappedData;
    if (setIt->second.frequency == UpdateFrequency::PerFrame) {
        basePtr = static_cast<char*>(basePtr) + (parent->getCurrentFrame() * setIt->second.alignedSize);
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
    if (setIt == parent->sets.end() || !setIt->second.mappedData) {
        Log::error("ShaderResources", "Buffer for set {} is not mapped", resourceInfo->set);
        return ElementProxy(nullptr, nullptr, 0, nullptr, 0);
    }

    uint32_t elementStride = bufferLayout->totalSize;

    void* basePtr = setIt->second.mappedData;
    if (setIt->second.frequency == UpdateFrequency::PerFrame) {
        basePtr = static_cast<char*>(basePtr) + (parent->getCurrentFrame() * setIt->second.alignedSize);
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
        return 0;
    }

    if (it->second.frequency == UpdateFrequency::PerFrame) {
        return frameIndex * it->second.alignedSize;
    }

    return 0;
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