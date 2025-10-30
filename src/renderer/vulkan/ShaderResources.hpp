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
class ElementProxy {
public:
    ElementProxy(ShaderResources* parent, const ReflectedResource* resourceInfo,
                 size_t elementIndex, void* bufferData, uint32_t elementStride);

    // Field access within array element
    FieldProxy operator[](const eastl::string& fieldName);

private:
    ShaderResources* parent;
    const ReflectedResource* resourceInfo;
    size_t elementIndex;
    void* bufferData;
    uint32_t elementStride;
};

// ===== ResourceProxy - Smart proxy for unified resource access =====
class ResourceProxy {
public:
    ResourceProxy(ShaderResources* parent, const ReflectedResource* resourceInfo);

    // UBO field access (only valid for UniformBuffer)
    FieldProxy operator[](const eastl::string& fieldName);

    // SSBO array element access (only valid for StorageBuffer)
    // Supports syntax: lights[i]["fieldName"] = value
    ElementProxy operator[](size_t elementIndex);

    // Unified assignment operators (auto-detect type from reflection)
    ResourceProxy& operator=(Texture* texture);
    ResourceProxy& operator=(const StorageBufferBinding& binding);
    ResourceProxy& operator=(vk::ImageView imageView);           // For StorageImage
    ResourceProxy& operator=(const BufferResource& buffer);      // For UniformBuffer
    ResourceProxy& operator=(vk::Sampler sampler);               // For Sampler

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
    // Constructor: Directly holds all data
    ShaderResources(
        eastl::string instanceName,
        eastl::shared_ptr<Shader> shader,
        ShaderReflection reflection,
        VulkanContext* context,
        uint32_t maxFrames,
        DescriptorManager* descriptorMgr = nullptr
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

    // Batch update multiple resources by name (reflection-driven)
    void updateResources(uint32_t setIndex,
                        const eastl::unordered_map<eastl::string, DescriptorResourceHandle>& resources);

    // === Descriptor Set Management ===

    // Get descriptor set by index
    vk::DescriptorSet getSet(uint32_t setIndex) const;

    // Get dynamic offset (only valid for PerFrame resources)
    uint32_t getDynamicOffset(uint32_t setIndex, uint32_t frameIndex) const;

    // Bind all descriptor sets to command buffer
    void bind(vk::CommandBuffer cmd, vk::PipelineLayout layout,
             vk::PipelineBindPoint bindPoint, uint32_t frameIndex = 0);

    // === Resource Query ===

    bool hasResource(const eastl::string& name) const;
    const ReflectedResource* getResourceInfo(const eastl::string& name) const;

    // === Instance Info ===

    const eastl::string& getInstanceName() const { return instanceName; }
    eastl::shared_ptr<Shader> getShader() const { return shader; }
    uint32_t getCurrentFrame() const { return currentFrame; }
    void setCurrentFrame(uint32_t frame) { currentFrame = frame; }

private:
    // Per-set data
    struct SetData {
        vk::DescriptorSet descriptorSet;
        uint32_t setIndex;
        LayoutHandle layoutHandle;
        UpdateFrequency frequency;
        bool isBindless;

        // Buffer data (for UBO/SSBO)
        bool hasBuffer = false;
        BufferResource buffer;
        uint32_t alignedSize = 0;  // Size per frame (for PerFrame frequency)
        void* mappedData = nullptr;
    };

    // Instance data
    eastl::string instanceName;
    eastl::shared_ptr<Shader> shader;
    ShaderReflection reflection;
    eastl::unordered_map<uint32_t, SetData> sets;

    // Context for GPU operations
    VulkanContext* context;
    DescriptorManager* descriptorManager;
    uint32_t maxFrames;
    uint32_t currentFrame = 0;
};

} // namespace violet