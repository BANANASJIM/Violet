#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/unordered_map.h>
#include <EASTL/shared_ptr.h>
#include "resource/shader/ShaderReflection.hpp"

namespace violet {

// Forward declarations
class VulkanContext;
class DescriptorManager;
class Shader;
class Texture;
struct BufferResource;
class FieldProxy;
class ResourceProxy;
class ShaderResources;

// Type alias for ShaderResources handle (must match DescriptorManager.hpp)
using ShaderResourcesHandle = uint64_t;

// Storage Buffer binding helper
struct StorageBufferBinding {
    vk::Buffer buffer;
    vk::DeviceSize offset = 0;
    vk::DeviceSize range = VK_WHOLE_SIZE;
};

// ===== ResourceProxy - Smart proxy for unified resource access =====
class ResourceProxy {
public:
    ResourceProxy(ShaderResources* parent, const ReflectedResource* resourceInfo);

    // UBO field access (only valid for UniformBuffer)
    FieldProxy operator[](const eastl::string& fieldName);

    // Unified assignment operators (auto-detect type from reflection)
    ResourceProxy& operator=(Texture* texture);
    ResourceProxy& operator=(const StorageBufferBinding& binding);
    ResourceProxy& operator=(vk::ImageView imageView);           // For StorageImage
    ResourceProxy& operator=(const BufferResource& buffer);      // For UniformBuffer

    // Query resource info
    vk::DescriptorType getType() const;
    const eastl::string& getName() const;
    bool isValid() const { return resourceInfo != nullptr; }

private:
    ShaderResources* parent;
    const ReflectedResource* resourceInfo;
};

// ===== ShaderResources - Lightweight proxy for centralized management =====
// 所有实际资源由 DescriptorManager 持有，此类只是访问接口
class ShaderResources {
    friend class ResourceProxy;

public:
    // 构造函数：只持有 handle 和 manager 引用
    ShaderResources(ShaderResourcesHandle handle, DescriptorManager* manager);

    // 析构函数：不需要清理资源（由 DescriptorManager 管理）
    ~ShaderResources() = default;

    // 禁止拷贝（资源由 DescriptorManager 唯一持有）
    ShaderResources(const ShaderResources&) = delete;
    ShaderResources& operator=(const ShaderResources&) = delete;

    // 允许移动
    ShaderResources(ShaderResources&&) = default;
    ShaderResources& operator=(ShaderResources&&) = default;

    // === Unified Resource Access ===

    // Access resource by name (returns proxy for chaining)
    ResourceProxy operator[](const eastl::string& resourceName);

    // === Descriptor Set Management ===

    // Get descriptor set by index
    vk::DescriptorSet getSet(uint32_t setIndex) const;

    // Get dynamic offset (only valid for PerFrame resources)
    uint32_t getDynamicOffset(uint32_t setIndex, uint32_t frameIndex) const;

    // Bind all descriptor sets to command buffer
    void bind(vk::CommandBuffer cmd, vk::PipelineLayout layout,
             vk::PipelineBindPoint bindPoint, uint32_t frameIndex = 0);

    // === Resource Queries ===

    bool hasResource(const eastl::string& name) const;
    const ReflectedResource* getResourceInfo(const eastl::string& name) const;

    // 获取实例信息
    const eastl::string& getInstanceName() const;
    eastl::shared_ptr<Shader> getShader() const;

    // 获取 handle（供内部使用）
    ShaderResourcesHandle getHandle() const { return handle; }

private:
    // 轻量级：只持有 handle 和 manager 指针
    ShaderResourcesHandle handle;
    DescriptorManager* manager;
};

} // namespace violet