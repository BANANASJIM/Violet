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
        return FieldProxy(nullptr, 0, 0, 1, 0, "invalid");
    }

    const ReflectedBuffer* bufferLayout = parent->bufferLayout;
    if (!bufferLayout) {
        Log::error("ShaderResources", "No buffer layout available");
        return FieldProxy(nullptr, 0, 0, 1, 0, "invalid");
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
        return FieldProxy(nullptr, 0, 0, 1, 0, "invalid");
    }

    uint32_t totalOffset = elementIndex * elementStride + field->offset;
    return FieldProxy(bufferData, totalOffset, field->size, field->arraySize, field->arrayStride, fieldName);
}

// ===== ResourceProxy Implementation =====

ResourceProxy::ResourceProxy(ShaderResources* parent, const ReflectedResource* resourceInfo)
    : parent(parent), resourceInfo(resourceInfo) {}

FieldProxy ResourceProxy::operator[](const eastl::string& fieldName) {
    if (!parent) {
        Log::error("ShaderResources", "Invalid resource proxy");
        return FieldProxy(nullptr, 0, 0, 1, 0, "invalid");
    }

    const ReflectedBuffer* bufferLayout = parent->bufferLayout;
    if (!bufferLayout) {
        Log::error("ShaderResources", "No buffer layout available");
        return FieldProxy(nullptr, 0, 0, 1, 0, "invalid");
    }

    // Find field in buffer layout
    const ReflectedField* field = nullptr;
    for (const auto& f : bufferLayout->fields) {
        if (f.name == fieldName) {
            field = &f;
            break;
        }
    }

    if (!field) {
        Log::error("ShaderResources", "Field '{}' not found in buffer",
                   fieldName.c_str());
        return FieldProxy(nullptr, 0, 0, 1, 0, "invalid");
    }

    // Calculate base pointer (handle PerFrame offset)
    void* basePtr = parent->mappedData;
    if (parent->frequency == UpdateFrequency::PerFrame) {
        basePtr = static_cast<char*>(basePtr) + (parent->getCurrentFrame() * parent->alignedSize);
    }

    return FieldProxy(basePtr, field->offset, field->size, field->arraySize, field->arrayStride, fieldName);
}

ElementProxy ResourceProxy::operator[](size_t elementIndex) {
    if (!parent) {
        Log::error("ShaderResources", "Invalid resource proxy - parent is null");
        return ElementProxy(nullptr, nullptr, 0, nullptr, 0);
    }

    if (!resourceInfo) {
        Log::error("ShaderResources", "Invalid resource proxy - resourceInfo is null (parent={}, instanceName={})",
                  (void*)parent, parent->getInstanceName().c_str());
        return ElementProxy(nullptr, nullptr, 0, nullptr, 0);
    }

    const ReflectedBuffer* bufferLayout = parent->bufferLayout;
    if (!bufferLayout) {
        Log::error("ShaderResources", "No buffer layout available");
        return ElementProxy(nullptr, nullptr, 0, nullptr, 0);
    }

    // Calculate base pointer (handle PerFrame offset)
    void* basePtr = parent->mappedData;
    if (parent->frequency == UpdateFrequency::PerFrame) {
        basePtr = static_cast<char*>(basePtr) + (parent->getCurrentFrame() * parent->alignedSize);
    }

    uint32_t elementStride = parent->elementSize;

    return ElementProxy(parent, resourceInfo, elementIndex, basePtr, elementStride);
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
    const ReflectedBuffer* bufferLayout,
    const ReflectedResource* resourceInfo,
    uint32_t setIndex,
    uint32_t binding,
    vk::DescriptorType descriptorType,
    UpdateFrequency frequency,
    VulkanContext* context,
    uint32_t maxFrames)
    : instanceName(eastl::move(instanceName))
    , bufferLayout(bufferLayout)
    , resourceInfo(resourceInfo)
    , setIndex(setIndex)
    , binding(binding)
    , frequency(frequency)
    , context(context)
    , maxFrames(maxFrames)
{
    // 1. Calculate buffer size
    elementSize = bufferLayout->totalSize;
    uint32_t arrayCount = (resourceInfo && resourceInfo->arraySize > 0) ? resourceInfo->arraySize : 1;
    uint32_t numCopies = (frequency == UpdateFrequency::PerFrame) ? maxFrames : 1;

    // 2. Calculate alignment (for PerFrame dynamic offsets)
    alignedSize = elementSize;
    if (frequency == UpdateFrequency::PerFrame) {
        uint32_t alignment = context->getMinUniformBufferOffsetAlignment();
        alignedSize = context->alignBufferSize(elementSize, alignment);
    }

    // 3. Create GPU buffer
    // Total size = alignedSize * arrayCount * numCopies
    // - alignedSize: single element size (with alignment if PerFrame)
    // - arrayCount: array capacity (e.g., 1024 for materials buffer)
    // - numCopies: frame count for PerFrame, 1 otherwise
    vk::BufferUsageFlags usage = (descriptorType == vk::DescriptorType::eUniformBuffer)
        ? vk::BufferUsageFlagBits::eUniformBuffer
        : vk::BufferUsageFlagBits::eStorageBuffer;

    BufferInfo bufferInfo{
        .size = alignedSize * arrayCount * numCopies,
        .usage = usage,
        .memoryUsage = MemoryUsage::CPU_TO_GPU,
        .debugName = this->instanceName
    };

    buffer = ResourceFactory::createBuffer(context, bufferInfo);
    mappedData = buffer.mappedData;

    Log::info("ShaderResources", "Created buffer '{}' (set={}, binding={}, size={} x {} x {} = {} bytes)",
              this->instanceName.c_str(), setIndex, binding, alignedSize, arrayCount, numCopies,
              alignedSize * arrayCount * numCopies);
}

ShaderResources::~ShaderResources() {
    if (buffer.buffer) {
        ResourceFactory::destroyBuffer(context, buffer);
    }
}

ResourceProxy ShaderResources::operator[](const eastl::string& resourceName) {
    // Single buffer container - resourceName is ignored (for compatibility)
    // Returns proxy for direct field/element access
    return ResourceProxy(this, resourceInfo);
}

eastl::unordered_map<BindingKey, DescriptorResourceHandle> ShaderResources::getBufferBindings() const {
    eastl::unordered_map<BindingKey, DescriptorResourceHandle> bindings;

    vk::DeviceSize offset = 0;
    vk::DeviceSize range = VK_WHOLE_SIZE;  // Bind entire buffer for descriptor

    // For PerFrame resources, use dynamic offset at bind time
    // Range is still VK_WHOLE_SIZE to cover all frames
    if (frequency == UpdateFrequency::PerFrame) {
        offset = currentFrame * alignedSize;
        // For PerFrame, we could use aligned frame size, but VK_WHOLE_SIZE is safer
    }

    bindings[BindingKey{setIndex, binding, 0}] =
        DescriptorResourceHandle::fromBuffer(
            buffer.buffer, offset, range, frequency
        );

    return bindings;
}

bool ShaderResources::hasResource(const eastl::string& name) const {
    // Single buffer - always return true if buffer exists
    return buffer.buffer != VK_NULL_HANDLE;
}

const ReflectedResource* ShaderResources::getResourceInfo(const eastl::string& name) const {
    // Not needed for single buffer container
    return nullptr;
}

} // namespace violet