#include "TransferPass.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "core/Log.hpp"

namespace violet {

void TransferPass::init(VulkanContext* ctx, const eastl::string& passName) {
    context = ctx;
    name = passName;

    violet::Log::info("Renderer", "Transfer pass '{}' initialized", name.c_str());
}

void TransferPass::cleanup() {
    context = nullptr;
}

void TransferPass::execute(vk::CommandBuffer cmd, uint32_t frameIndex) {
    // Execute custom transfer operations via callback
    if (executeCallback) {
        executeCallback(cmd, frameIndex);
    }
}

} // namespace violet