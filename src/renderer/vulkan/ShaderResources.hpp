#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/unordered_map.h>
#include <EASTL/shared_ptr.h>
#include "resource/shader/ShaderReflection.hpp"
#include "resource/gpu/ResourceFactory.hpp"
#include "renderer/vulkan/DescriptorManager.hpp"
#include "core/Log.hpp"

namespace violet {

// Forward declarations
class VulkanContext;
class Shader;
class Texture;
class ElementProxy;
class ResourceProxy;
class ShaderResources;

// Storage Buffer binding helper
struct StorageBufferBinding {
    vk::Buffer buffer;
    vk::DeviceSize offset = 0;
    vk::DeviceSize range = VK_WHOLE_SIZE;
};

// ===== FieldProxy - Type-safe field access in UBO/SSBO =====
class FieldProxy {
public:
    FieldProxy(void* bufferData, uint32_t offset, uint32_t size, const eastl::string& fieldName)
        : bufferData(bufferData), offset(offset), size(size), fieldName(fieldName) {}

    // Type-safe assignment operator with trivially copyable constraint
    template<typename T>
    requires std::is_trivially_copyable_v<T>
    FieldProxy& operator=(const T& value) noexcept {
        if (sizeof(T) != size) {
            Log::error("ShaderResources",
                "Size mismatch for field '{}': expected {} bytes, got {} bytes",
                fieldName.c_str(), size, sizeof(T));
            return *this;
        }

        if (bufferData) {
            memcpy(static_cast<uint8_t*>(bufferData) + offset, &value, sizeof(T));
        }
        return *this;
    }

private:
    void* bufferData;
    uint32_t offset;
    uint32_t size;
    eastl::string fieldName;
};

// ===== ElementProxy - Proxy for array element access in SSBO =====
// Supports syntax: lights[i]["fieldName"] = value
// or direct struct assignment: lights[i] = lightData
class ElementProxy {
public:
    ElementProxy(ShaderResources* parent, const ReflectedResource* resourceInfo,
                 size_t elementIndex, void* bufferData, uint32_t elementStride);

    // Field access within array element
    FieldProxy operator[](const eastl::string& fieldName);

    // Direct struct assignment (for complex types with arrays)
    template<typename T>
    requires std::is_trivially_copyable_v<T>
    ElementProxy& operator=(const T& value) noexcept {
        if (!bufferData || !parent) {
            Log::error("ShaderResources", "Invalid element proxy");
            return *this;
        }

        // Verify size matches element stride (with some tolerance for padding)
        if (sizeof(T) > elementStride) {
            Log::error("ShaderResources",
                "Struct size mismatch: struct is {} bytes but element stride is {} bytes",
                sizeof(T), elementStride);
            return *this;
        }

        // Copy entire struct to this element's location
        char* destPtr = static_cast<char*>(bufferData) + (elementIndex * elementStride);
        memcpy(destPtr, &value, sizeof(T));
        return *this;
    }

private:
    ShaderResources* parent;
    const ReflectedResource* resourceInfo;
    size_t elementIndex;
    void* bufferData;
    uint32_t elementStride;
};

// ===== ResourceProxy - Proxy for buffer field/element access =====
class ResourceProxy {
public:
    ResourceProxy(ShaderResources* parent, const ReflectedResource* resourceInfo);

    // UBO field access (only valid for UniformBuffer)
    FieldProxy operator[](const eastl::string& fieldName);

    // SSBO array element access (only valid for StorageBuffer)
    // Supports syntax: lights[i]["fieldName"] = value
    ElementProxy operator[](size_t elementIndex);

    // Query resource info
    vk::DescriptorType getType() const;
    const eastl::string& getName() const;
    bool isValid() const { return resourceInfo != nullptr; }

private:
    ShaderResources* parent;
    const ReflectedResource* resourceInfo;
};

// ===== ShaderResources - Direct data holder for shader resources =====
class ShaderResources {
    friend class ResourceProxy;
    friend class ElementProxy;
    friend class ResourceManager;  // Factory access

public:
    // Constructor: Creates buffer and initializes all internal structures
    ShaderResources(
        eastl::string instanceName,
        const ReflectedBuffer* bufferLayout,
        uint32_t setIndex,
        uint32_t binding,
        vk::DescriptorType descriptorType,
        UpdateFrequency frequency,
        VulkanContext* context,
        uint32_t maxFrames
    );

    // Destructor: Cleanup GPU resources
    ~ShaderResources();

    // Non-copyable (owns GPU resources)
    ShaderResources(const ShaderResources&) = delete;
    ShaderResources& operator=(const ShaderResources&) = delete;

    // Movable
    ShaderResources(ShaderResources&&) noexcept = default;
    ShaderResources& operator=(ShaderResources&&) noexcept = default;

    // === Unified Resource Access ===

    // Access resource by name (returns proxy for chaining)
    ResourceProxy operator[](const eastl::string& resourceName);

    // === Buffer Bindings (for use with ShaderResourceBinding) ===

    // Get all buffer bindings as DescriptorResourceHandles
    // Returns map of (set, binding) -> buffer handle for all buffers owned by this instance
    eastl::unordered_map<BindingKey, DescriptorResourceHandle> getBufferBindings() const;

    // === Resource Query ===

    bool hasResource(const eastl::string& name) const;
    const ReflectedResource* getResourceInfo(const eastl::string& name) const;

    // === Instance Info ===

    const eastl::string& getInstanceName() const { return instanceName; }
    uint32_t getCurrentFrame() const { return currentFrame; }
    void setCurrentFrame(uint32_t frame) { currentFrame = frame; }

private:
    // Instance data
    eastl::string instanceName;
    const ReflectedBuffer* bufferLayout;  // Layout for field access

    // Binding info
    uint32_t setIndex;
    uint32_t binding;
    UpdateFrequency frequency;

    // Buffer data (single buffer per ShaderResources)
    BufferResource buffer;
    uint32_t alignedSize = 0;   // Size per frame (for PerFrame frequency)
    void* mappedData = nullptr;
    uint32_t elementSize = 0;   // bufferLayout->totalSize

    // Context for GPU operations
    VulkanContext* context;
    uint32_t maxFrames;
    uint32_t currentFrame = 0;
};

} // namespace violet