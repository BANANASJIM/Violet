#include "ComputePass.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "core/Log.hpp"

namespace violet {

void ComputePass::init(VulkanContext* ctx, const eastl::string& n) {
    context = ctx;
    name = n;
}

void ComputePass::execute(vk::CommandBuffer cmd, uint32_t frameIndex) {
    // Execute user callback (callback is responsible for binding pipeline and dispatching)
    if (executeCallback) {
        executeCallback(cmd, frameIndex);
    }
}

void ComputePass::cleanup() {
    executeCallback = nullptr;
}

} // namespace violet