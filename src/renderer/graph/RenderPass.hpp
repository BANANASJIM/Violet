#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>
#include <EASTL/string.h>
#include <EASTL/functional.h>
#include "Pass.hpp"

namespace violet {

class VulkanContext;

class RenderPass : public Pass {
public:
    void init(VulkanContext* ctx, vk::RenderPass renderPass, const eastl::string& name);
    void cleanup() override;

    void execute(vk::CommandBuffer cmd, uint32_t frameIndex) override;

    PassType getType() const override { return PassType::Graphics; }
    const eastl::string& getName() const override { return name; }

    const eastl::vector<ResourceHandle>& getReadResources() const override { return reads; }
    const eastl::vector<ResourceHandle>& getWriteResources() const override { return writes; }

    void setReadResources(const eastl::vector<ResourceHandle>& r) { reads = r; }
    void setWriteResources(const eastl::vector<ResourceHandle>& w) { writes = w; }

    void setFramebuffer(vk::Framebuffer fb) { framebuffer = fb; }
    void setRenderArea(vk::Extent2D extent) { renderArea = extent; }
    void setClearValues(const eastl::vector<vk::ClearValue>& cv) { clearValues = cv; }

    // Execution callback
    void setExecuteCallback(eastl::function<void(vk::CommandBuffer, uint32_t)> cb) {
        executeCallback = eastl::move(cb);
    }

    vk::RenderPass getRenderPass() const { return renderPass; }

private:
    VulkanContext* context = nullptr;
    eastl::string name;
    vk::RenderPass renderPass;  
    vk::Framebuffer framebuffer = VK_NULL_HANDLE;  
    vk::Extent2D renderArea = {};
    eastl::vector<vk::ClearValue> clearValues;

    eastl::vector<ResourceHandle> reads;
    eastl::vector<ResourceHandle> writes;

    eastl::function<void(vk::CommandBuffer, uint32_t)> executeCallback;
};

} // namespace violet