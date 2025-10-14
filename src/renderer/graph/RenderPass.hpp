#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>
#include <EASTL/string.h>
#include <EASTL/functional.h>
#include "Pass.hpp"

namespace violet {

class VulkanContext;

// RenderPass for RenderGraph - uses dynamic rendering (no vk::RenderPass/Framebuffer)
class RenderPass : public Pass {
public:
    void init(VulkanContext* ctx, const eastl::string& name);
    void cleanup() override;

    void execute(vk::CommandBuffer cmd, uint32_t frameIndex) override;

    PassType getType() const override { return PassType::Graphics; }
    const eastl::string& getName() const override { return name; }

    const eastl::vector<ResourceHandle>& getReadResources() const override { return reads; }
    const eastl::vector<ResourceHandle>& getWriteResources() const override { return writes; }

    void setReadResources(const eastl::vector<ResourceHandle>& r) { reads = r; }
    void setWriteResources(const eastl::vector<ResourceHandle>& w) { writes = w; }

    // Execution callback - user code renders inside beginRendering/endRendering
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