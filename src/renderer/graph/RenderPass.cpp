#include "RenderPass.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "core/Log.hpp"

namespace violet {

void RenderPass::init(VulkanContext* ctx, const eastl::string& n) {
    context = ctx;
    name = n;
}

void RenderPass::execute(vk::CommandBuffer cmd, uint32_t frameIndex) {
    // Execute user callback
    // User code should call cmd.beginRendering()/endRendering() inside callback
    if (executeCallback) {
        executeCallback(cmd, frameIndex);
    }
}

void RenderPass::cleanup() {
    // No Vulkan resources to clean up (dynamic rendering)
    executeCallback = nullptr;
}

} // namespace violet