#pragma once

#include "Pass.hpp"
#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/functional.h>

namespace violet {

class VulkanContext;

class ComputePass : public Pass {
public:
    ComputePass() = default;
    ~ComputePass() override { cleanup(); }

    void init(VulkanContext* ctx, const eastl::string& name);
    void cleanup() override;

    void execute(vk::CommandBuffer cmd, uint32_t frameIndex) override;

    PassType getType() const override { return PassType::Compute; }
    const eastl::string& getName() const override { return name; }

    const eastl::vector<ResourceHandle>& getReadResources() const override { return reads; }
    const eastl::vector<ResourceHandle>& getWriteResources() const override { return writes; }

    void setReadResources(const eastl::vector<ResourceHandle>& r) { reads = r; }
    void setWriteResources(const eastl::vector<ResourceHandle>& w) { writes = w; }

    void setExecuteCallback(eastl::function<void(vk::CommandBuffer, uint32_t)> cb) {
        executeCallback = eastl::move(cb);
    }

private:
    VulkanContext* context = nullptr;
    eastl::string name;

    eastl::vector<ResourceHandle> reads;
    eastl::vector<ResourceHandle> writes;

    eastl::function<void(vk::CommandBuffer, uint32_t)> executeCallback;
};

} // namespace violet
