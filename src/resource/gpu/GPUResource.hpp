#pragma once

#include <vk_mem_alloc.h>
#include <EASTL/string.h>

namespace violet {

class VulkanContext;

class GPUResource {
public:
GPUResource() = default;
    virtual ~GPUResource() = default;

    GPUResource(const GPUResource&) = delete;
    GPUResource& operator=(const GPUResource&) = delete;

    GPUResource(GPUResource&& other) noexcept
        : context(other.context)
        , allocation(other.allocation)
        , debugName(eastl::move(other.debugName)) {
        other.context = nullptr;
        other.allocation = VK_NULL_HANDLE;
    }

    GPUResource& operator=(GPUResource&& other) noexcept {
        if (this != &other) {
            cleanup();
            context = other.context;
            allocation = other.allocation;
            debugName = eastl::move(other.debugName);
            other.context = nullptr;
            other.allocation = VK_NULL_HANDLE;
        }
        return *this;
    }

    virtual void cleanup() = 0;

    void setDebugName(const eastl::string& name) { debugName = name; }
    const eastl::string& getDebugName() const { return debugName; }

    VmaAllocation getAllocation() const { return allocation; }
    bool isValid() const { return allocation != VK_NULL_HANDLE && context != nullptr; }

protected:
    VulkanContext* context = nullptr;
    VmaAllocation allocation = VK_NULL_HANDLE;
    eastl::string debugName;
};

} // namespace violet