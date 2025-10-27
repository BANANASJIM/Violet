#include "ShaderResources.hpp"
#include "DescriptorManager.hpp"
#include "VulkanContext.hpp"
#include "resource/shader/Shader.hpp"
#include "resource/Texture.hpp"
#include "core/Log.hpp"

namespace violet {

// ===== ResourceProxy Implementation =====

ResourceProxy::ResourceProxy(ShaderResources* parent, const ReflectedResource* resourceInfo)
    : parent(parent), resourceInfo(resourceInfo) {}

FieldProxy ResourceProxy::operator[](const eastl::string& fieldName) {
    if (!resourceInfo) {
        Log::error("ShaderResources", "Invalid resource proxy");
        return FieldProxy(nullptr, 0, 0, "invalid");
    }

    if (resourceInfo->type != vk::DescriptorType::eUniformBuffer &&
        resourceInfo->type != vk::DescriptorType::eStorageBuffer) {
        Log::error("ShaderResources", "Resource '{}' is not a buffer (cannot access fields)",
                   resourceInfo->name.c_str());
        return FieldProxy(nullptr, 0, 0, "invalid");
    }

    // Get buffer layout
    const ReflectedBuffer* bufferLayout = resourceInfo->bufferLayout;
    if (!bufferLayout) {
        Log::error("ShaderResources", "Resource '{}' has no buffer layout",
                   resourceInfo->name.c_str());
        return FieldProxy(nullptr, 0, 0, "invalid");
    }

    // Find field
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

    // Get managed data from DescriptorManager
    auto* data = parent->manager->getShaderResourcesData(parent->handle);
    if (!data) {
        Log::error("ShaderResources", "Invalid ShaderResources handle");
        return FieldProxy(nullptr, 0, 0, "invalid");
    }

    auto setIt = data->sets.find(resourceInfo->set);
    if (setIt == data->sets.end() || !setIt->second.mappedData) {
        Log::error("ShaderResources", "Buffer for set {} is not mapped", resourceInfo->set);
        return FieldProxy(nullptr, 0, 0, "invalid");
    }

    // For PerFrame buffers, use current frame offset
    void* basePtr = setIt->second.mappedData;
    if (setIt->second.frequency == UpdateFrequency::PerFrame) {
        uint32_t frameIndex = parent->manager->getCurrentFrame();
        basePtr = static_cast<char*>(basePtr) + (frameIndex * setIt->second.alignedSize);
    }

    return FieldProxy(basePtr, field->offset, field->size, fieldName);
}

ResourceProxy& ResourceProxy::operator=(Texture* texture) {
    if (!resourceInfo || resourceInfo->type != vk::DescriptorType::eCombinedImageSampler) {
        Log::error("ShaderResources", "Resource '{}' is not a CombinedImageSampler",
                   resourceInfo ? resourceInfo->name.c_str() : "null");
        return *this;
    }

    auto* data = parent->manager->getShaderResourcesData(parent->handle);
    if (!data) {
        Log::error("ShaderResources", "Invalid ShaderResources handle");
        return *this;
    }

    auto setIt = data->sets.find(resourceInfo->set);
    if (setIt == data->sets.end()) {
        Log::error("ShaderResources", "Set {} not found", resourceInfo->set);
        return *this;
    }

    // Update descriptor
    vk::DescriptorImageInfo imageInfo;
    imageInfo.imageView = texture->getImageView();
    imageInfo.sampler = texture->getSampler();
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::WriteDescriptorSet write;
    write.dstSet = setIt->second.descriptorSet;
    write.dstBinding = resourceInfo->binding;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    // Update through VulkanContext (accessed via manager)
    parent->manager->getContext()->getDevice().updateDescriptorSets({write}, {});

    return *this;
}

ResourceProxy& ResourceProxy::operator=(const StorageBufferBinding& binding) {
    if (!resourceInfo || resourceInfo->type != vk::DescriptorType::eStorageBuffer) {
        Log::error("ShaderResources", "Resource '{}' is not a StorageBuffer",
                   resourceInfo ? resourceInfo->name.c_str() : "null");
        return *this;
    }

    auto* data = parent->manager->getShaderResourcesData(parent->handle);
    if (!data) {
        return *this;
    }

    auto setIt = data->sets.find(resourceInfo->set);
    if (setIt == data->sets.end()) {
        return *this;
    }

    vk::DescriptorBufferInfo bufferInfo;
    bufferInfo.buffer = binding.buffer;
    bufferInfo.offset = binding.offset;
    bufferInfo.range = binding.range;

    vk::WriteDescriptorSet write;
    write.dstSet = setIt->second.descriptorSet;
    write.dstBinding = resourceInfo->binding;
    write.descriptorType = vk::DescriptorType::eStorageBuffer;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;

    parent->manager->getContext()->getDevice().updateDescriptorSets({write}, {});

    return *this;
}

ResourceProxy& ResourceProxy::operator=(vk::ImageView imageView) {
    if (!resourceInfo || resourceInfo->type != vk::DescriptorType::eStorageImage) {
        Log::error("ShaderResources", "Resource '{}' is not a StorageImage",
                   resourceInfo ? resourceInfo->name.c_str() : "null");
        return *this;
    }

    auto* data = parent->manager->getShaderResourcesData(parent->handle);
    if (!data) {
        return *this;
    }

    auto setIt = data->sets.find(resourceInfo->set);
    if (setIt == data->sets.end()) {
        return *this;
    }

    vk::DescriptorImageInfo imageInfo;
    imageInfo.imageView = imageView;
    imageInfo.imageLayout = vk::ImageLayout::eGeneral;
    imageInfo.sampler = nullptr;

    vk::WriteDescriptorSet write;
    write.dstSet = setIt->second.descriptorSet;
    write.dstBinding = resourceInfo->binding;
    write.descriptorType = vk::DescriptorType::eStorageImage;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    parent->manager->getContext()->getDevice().updateDescriptorSets({write}, {});

    return *this;
}

ResourceProxy& ResourceProxy::operator=(const BufferResource& buffer) {
    if (!resourceInfo || resourceInfo->type != vk::DescriptorType::eUniformBuffer) {
        Log::error("ShaderResources", "Resource '{}' is not a UniformBuffer",
                   resourceInfo ? resourceInfo->name.c_str() : "null");
        return *this;
    }

    auto* data = parent->manager->getShaderResourcesData(parent->handle);
    if (!data) {
        return *this;
    }

    auto setIt = data->sets.find(resourceInfo->set);
    if (setIt == data->sets.end()) {
        return *this;
    }

    vk::DescriptorBufferInfo bufferInfo;
    bufferInfo.buffer = buffer.buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = VK_WHOLE_SIZE;

    vk::WriteDescriptorSet write;
    write.dstSet = setIt->second.descriptorSet;
    write.dstBinding = resourceInfo->binding;
    write.descriptorType = vk::DescriptorType::eUniformBuffer;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;

    parent->manager->getContext()->getDevice().updateDescriptorSets({write}, {});

    return *this;
}

vk::DescriptorType ResourceProxy::getType() const {
    return resourceInfo ? resourceInfo->type : vk::DescriptorType::eUniformBuffer;
}

const eastl::string& ResourceProxy::getName() const {
    static eastl::string empty;
    return resourceInfo ? resourceInfo->name : empty;
}

// ===== ShaderResources Implementation (Lightweight Proxy) =====

ShaderResources::ShaderResources(ShaderResourcesHandle handle, DescriptorManager* manager)
    : handle(handle), manager(manager) {}

ResourceProxy ShaderResources::operator[](const eastl::string& resourceName) {
    auto* data = manager->getShaderResourcesData(handle);
    if (!data || !data->reflection) {
        Log::error("ShaderResources", "No reflection data available");
        return ResourceProxy(this, nullptr);
    }

    const ReflectedResource* resource = data->reflection->findResource(resourceName);
    if (!resource) {
        Log::error("ShaderResources", "Resource '{}' not found in shader '{}'",
                   resourceName.c_str(), data->shader->getName().c_str());
        return ResourceProxy(this, nullptr);
    }

    return ResourceProxy(this, resource);
}

vk::DescriptorSet ShaderResources::getSet(uint32_t setIndex) const {
    auto* data = manager->getShaderResourcesData(handle);
    if (!data) {
        return nullptr;
    }

    auto it = data->sets.find(setIndex);
    if (it == data->sets.end()) {
        return nullptr;
    }
    return it->second.descriptorSet;
}

uint32_t ShaderResources::getDynamicOffset(uint32_t setIndex, uint32_t frameIndex) const {
    auto* data = manager->getShaderResourcesData(handle);
    if (!data) {
        return 0;
    }

    auto it = data->sets.find(setIndex);
    if (it == data->sets.end()) {
        return 0;
    }

    // Return dynamic offset for PerFrame buffers
    if (it->second.frequency == UpdateFrequency::PerFrame) {
        return frameIndex * it->second.alignedSize;
    }

    return 0;
}

void ShaderResources::bind(vk::CommandBuffer cmd, vk::PipelineLayout layout,
                           vk::PipelineBindPoint bindPoint, uint32_t frameIndex) {
    auto* data = manager->getShaderResourcesData(handle);
    if (!data || data->sets.empty()) {
        return;
    }

    // Bind each descriptor set
    for (auto& [setIndex, setData] : data->sets) {
        eastl::vector<uint32_t> dynamicOffsets;

        // Add dynamic offset for PerFrame buffers
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
    auto* data = manager->getShaderResourcesData(handle);
    return data && data->reflection && data->reflection->findResource(name) != nullptr;
}

const ReflectedResource* ShaderResources::getResourceInfo(const eastl::string& name) const {
    auto* data = manager->getShaderResourcesData(handle);
    return (data && data->reflection) ? data->reflection->findResource(name) : nullptr;
}

const eastl::string& ShaderResources::getInstanceName() const {
    auto* data = manager->getShaderResourcesData(handle);
    static eastl::string empty;
    return data ? data->instanceName : empty;
}

eastl::shared_ptr<Shader> ShaderResources::getShader() const {
    auto* data = manager->getShaderResourcesData(handle);
    return data ? data->shader : nullptr;
}

} // namespace violet
