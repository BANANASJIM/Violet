#include "RenderPass.hpp"
#include "RenderPassBuilder.hpp"
#include "VulkanContext.hpp"
#include <EASTL/array.h>

namespace violet {

void RenderPass::init(VulkanContext* ctx, vk::Format colorFormat) {
    context = ctx;

    eastl::vector<vk::AttachmentDescription> attachments(2);

    // Color attachment
    attachments[0].format = colorFormat;
    attachments[0].samples = vk::SampleCountFlagBits::e1;
    attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
    attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
    attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    attachments[0].initialLayout = vk::ImageLayout::eUndefined;
    attachments[0].finalLayout = vk::ImageLayout::ePresentSrcKHR;

    // Depth attachment
    attachments[1].format = ctx->findDepthFormat();
    attachments[1].samples = vk::SampleCountFlagBits::e1;
    attachments[1].loadOp = vk::AttachmentLoadOp::eClear;
    attachments[1].storeOp = vk::AttachmentStoreOp::eDontCare;
    attachments[1].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    attachments[1].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    attachments[1].initialLayout = vk::ImageLayout::eUndefined;
    attachments[1].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::AttachmentReference colorAttachmentRef;
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::AttachmentReference depthAttachmentRef;
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::SubpassDescription subpass;
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    vk::SubpassDependency dependency;
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependency.srcAccessMask = {};
    dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

    vk::RenderPassCreateInfo renderPassInfo;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    renderPass = context->getDevice().createRenderPass(renderPassInfo);
}

void RenderPass::init(VulkanContext* ctx, RenderPassBuilder& builder) {
    context = ctx;
    renderPass = builder.build(context);
}

void RenderPass::init(VulkanContext* ctx, const PassConfig& cfg) {
    context = ctx;
    config = cfg;

    // Build attachment references
    eastl::vector<vk::AttachmentReference> colorRefs;
    for (size_t i = 0; i < config.colorAttachments.size(); ++i) {
        vk::AttachmentReference ref;
        ref.attachment = static_cast<uint32_t>(i);
        ref.layout = vk::ImageLayout::eColorAttachmentOptimal;
        colorRefs.push_back(ref);
    }

    vk::AttachmentReference depthRef;
    if (config.hasDepth) {
        depthRef.attachment = static_cast<uint32_t>(config.colorAttachments.size());
        depthRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    }

    // Create subpass
    vk::SubpassDescription subpass;
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
    subpass.pColorAttachments = colorRefs.data();
    subpass.pDepthStencilAttachment = config.hasDepth ? &depthRef : nullptr;

    // Subpass dependency
    vk::SubpassDependency dependency;
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = config.srcStage | (config.hasDepth ? vk::PipelineStageFlagBits::eEarlyFragmentTests : vk::PipelineStageFlags{});
    dependency.dstStageMask = config.dstStage | (config.hasDepth ? vk::PipelineStageFlagBits::eEarlyFragmentTests : vk::PipelineStageFlags{});
    dependency.srcAccessMask = config.srcAccess;
    dependency.dstAccessMask = config.dstAccess | (config.hasDepth ? vk::AccessFlagBits::eDepthStencilAttachmentWrite : vk::AccessFlags{});

    // Combine attachments
    eastl::vector<vk::AttachmentDescription> attachments = config.colorAttachments;
    if (config.hasDepth) {
        attachments.push_back(config.depthAttachment);
    }

    // Create render pass
    vk::RenderPassCreateInfo renderPassInfo;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    renderPass = context->getDevice().createRenderPass(renderPassInfo);
}

void RenderPass::begin(vk::CommandBuffer cmd, vk::Framebuffer framebuffer, vk::Extent2D extent) {
    vk::RenderPassBeginInfo beginInfo;
    beginInfo.renderPass = renderPass;
    beginInfo.framebuffer = framebuffer;
    beginInfo.renderArea.offset = vk::Offset2D{0, 0};
    beginInfo.renderArea.extent = extent;
    beginInfo.clearValueCount = static_cast<uint32_t>(config.clearValues.size());
    beginInfo.pClearValues = config.clearValues.data();

    cmd.beginRenderPass(beginInfo, vk::SubpassContents::eInline);
}

void RenderPass::execute(vk::CommandBuffer cmd, uint32_t frameIndex) {
    if (config.execute) {
        config.execute(cmd, frameIndex);
    }
}

void RenderPass::end(vk::CommandBuffer cmd) {
    cmd.endRenderPass();
}

void RenderPass::cleanup() {
    if (renderPass) {
        context->getDevice().destroyRenderPass(renderPass);
        renderPass = nullptr;
    }
}

void RenderPass::init(VulkanContext* context, const RenderPassDesc& desc) {
    this->context = context;

    // Convert RenderPassDesc to PassConfig for compatibility
    config.name = desc.name;
    config.colorAttachments = desc.colorAttachments;
    config.hasDepth = desc.hasDepth;
    config.depthAttachment = desc.depthAttachment;
    config.clearValues = desc.clearValues;
    config.execute = desc.execute;

    // Update final layouts based on desc hints
    if (!config.colorAttachments.empty()) {
        config.colorAttachments.back().finalLayout = desc.finalColorLayout;
    }
    if (config.hasDepth) {
        config.depthAttachment.finalLayout = desc.finalDepthLayout;
    }

    // Use existing init logic
    init(context, config);
}

void RenderPass::insertImageBarrier(
    vk::CommandBuffer cmd,
    vk::ImageLayout oldLayout,
    vk::ImageLayout newLayout,
    vk::PipelineStageFlags srcStage,
    vk::PipelineStageFlags dstStage,
    vk::AccessFlags srcAccess,
    vk::AccessFlags dstAccess
) {
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = VK_NULL_HANDLE;  // Will be set for specific images when needed
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    // Set access masks based on layouts if not specified
    if (!srcAccess) {
        if (oldLayout == vk::ImageLayout::eColorAttachmentOptimal) {
            srcAccess = vk::AccessFlagBits::eColorAttachmentWrite;
        } else if (oldLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
            srcAccess = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        }
    }

    if (!dstAccess) {
        if (newLayout == vk::ImageLayout::eColorAttachmentOptimal) {
            dstAccess = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
        } else if (newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
            dstAccess = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        } else if (newLayout == vk::ImageLayout::ePresentSrcKHR) {
            dstAccess = vk::AccessFlagBits::eMemoryRead;
        }
    }

    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    cmd.pipelineBarrier(srcStage, dstStage, {}, {}, {}, barrier);
}

void RenderPass::insertMemoryBarrier(
    vk::CommandBuffer cmd,
    vk::PipelineStageFlags srcStage,
    vk::PipelineStageFlags dstStage,
    vk::AccessFlags srcAccess,
    vk::AccessFlags dstAccess
) {
    vk::MemoryBarrier barrier{};
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    cmd.pipelineBarrier(srcStage, dstStage, {}, barrier, {}, {});
}

}