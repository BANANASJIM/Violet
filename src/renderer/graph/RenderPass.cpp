#include "RenderPass.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "core/Log.hpp"

namespace violet {

void RenderPass::init(VulkanContext* ctx, vk::RenderPass rp, const eastl::string& n) {
    context = ctx;
    renderPass = rp;
    name = n;
}

void RenderPass::execute(vk::CommandBuffer cmd, uint32_t frameIndex) {
    if (!renderPass || !framebuffer) {
        Log::error("RenderPass", "Cannot execute '{}': renderPass or framebuffer not set", name.c_str());
        return;
    }

    // Begin render pass
    vk::RenderPassBeginInfo beginInfo;
    beginInfo.renderPass = renderPass;
    beginInfo.framebuffer = framebuffer;
    beginInfo.renderArea.offset = vk::Offset2D{0, 0};
    beginInfo.renderArea.extent = renderArea;
    beginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    beginInfo.pClearValues = clearValues.data();

    cmd.beginRenderPass(beginInfo, vk::SubpassContents::eInline);

    // Execute user callback
    if (executeCallback) {
        executeCallback(cmd, frameIndex);
    }

    // End render pass
    cmd.endRenderPass();
}

void RenderPass::cleanup() {
    // RenderPass and framebuffer are owned by RenderGraph, don't destroy here
    renderPass = nullptr;
    framebuffer = VK_NULL_HANDLE;
}

} // namespace violet