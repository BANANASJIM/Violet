#include "ComputePass.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "core/Log.hpp"

namespace violet {

void ComputePass::init(VulkanContext* ctx, ComputePipeline* pipe, const eastl::string& n) {
    context = ctx;
    pipeline = pipe;
    name = n;
}

void ComputePass::execute(vk::CommandBuffer cmd, uint32_t frameIndex) {
    if (!pipeline) {
        Log::error("ComputePass", "Cannot execute '{}': pipeline not set", name.c_str());
        return;
    }

    // Bind compute pipeline
    pipeline->bind(cmd);

    // Execute user callback
    if (executeCallback) {
        executeCallback(cmd, frameIndex);
    }
}

void ComputePass::cleanup() {
    // Pipeline is not owned by ComputePass, don't destroy
    pipeline = nullptr;
}

} // namespace violet