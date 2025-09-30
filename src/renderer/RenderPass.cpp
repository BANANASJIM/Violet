#include "RenderPass.hpp"
#include "VulkanContext.hpp"
#include "Texture.hpp"
#include "ResourceFactory.hpp"
#include <EASTL/array.h>

namespace violet {

// AttachmentDesc factory methods
AttachmentDesc AttachmentDesc::color(vk::Format fmt, vk::AttachmentLoadOp load) {
    AttachmentDesc desc;
    desc.format = fmt;
    desc.loadOp = load;
    desc.storeOp = vk::AttachmentStoreOp::eStore;
    desc.initialLayout = (load == vk::AttachmentLoadOp::eLoad) ?
        vk::ImageLayout::eColorAttachmentOptimal : vk::ImageLayout::eUndefined;
    desc.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
    return desc;
}

AttachmentDesc AttachmentDesc::depth(vk::Format fmt, vk::AttachmentLoadOp load) {
    AttachmentDesc desc;
    desc.format = fmt;
    desc.loadOp = load;
    desc.storeOp = vk::AttachmentStoreOp::eDontCare;
    desc.initialLayout = (load == vk::AttachmentLoadOp::eLoad) ?
        vk::ImageLayout::eDepthStencilAttachmentOptimal : vk::ImageLayout::eUndefined;
    desc.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    return desc;
}

AttachmentDesc AttachmentDesc::swapchainColor(vk::Format fmt, vk::AttachmentLoadOp load) {
    AttachmentDesc desc;
    desc.format = fmt;
    desc.loadOp = load;
    desc.storeOp = vk::AttachmentStoreOp::eStore;
    desc.initialLayout = (load == vk::AttachmentLoadOp::eLoad) ?
        vk::ImageLayout::eColorAttachmentOptimal : vk::ImageLayout::eUndefined;
    desc.finalLayout = vk::ImageLayout::ePresentSrcKHR;
    return desc;
}

vk::AttachmentDescription AttachmentDesc::toVulkan() const {
    vk::AttachmentDescription vulkanDesc;
    vulkanDesc.format = format;
    vulkanDesc.samples = samples;
    vulkanDesc.loadOp = loadOp;
    vulkanDesc.storeOp = storeOp;
    vulkanDesc.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    vulkanDesc.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    vulkanDesc.initialLayout = initialLayout;
    vulkanDesc.finalLayout = finalLayout;
    return vulkanDesc;
}

void RenderPass::init(VulkanContext* ctx, const RenderPassConfig& cfg) {
    context = ctx;
    config = cfg;

    // Convert AttachmentDesc to Vulkan attachments
    eastl::vector<vk::AttachmentDescription> attachments;
    for (const auto& colorAttach : config.colorAttachments) {
        attachments.push_back(colorAttach.toVulkan());
    }
    if (config.hasDepth) {
        attachments.push_back(config.depthAttachment.toVulkan());
    }

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
    cleanupFramebuffers();

    if (renderPass) {
        context->getDevice().destroyRenderPass(renderPass);
        renderPass = nullptr;
    }
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

void RenderPass::setExternalFramebuffer(vk::Framebuffer framebuffer) {
    externalFramebuffer = framebuffer;
}

void RenderPass::begin(vk::CommandBuffer cmd, vk::Extent2D extent) {
    // Use external framebuffer if configured for swapchain
    if (config.isSwapchainPass && externalFramebuffer != VK_NULL_HANDLE) {
        begin(cmd, externalFramebuffer, extent);
    }
    // Use own framebuffer
    else if (config.createOwnFramebuffer && !framebuffers.empty()) {
        begin(cmd, framebuffers[0], currentExtent);
    }
}

void RenderPass::onSwapchainRecreate(vk::Extent2D newSize) {
    // If this pass creates its own framebuffers and follows swapchain size, recreate them
    if (config.createOwnFramebuffer && config.followsSwapchainSize) {
        recreateFramebuffers(newSize);
    }
}

void RenderPass::createFramebuffers(vk::Extent2D extent) {
    if (!config.createOwnFramebuffer) {
        return; // This pass uses external framebuffers
    }

    currentExtent = extent;

    // Determine actual extent
    vk::Extent2D actualExtent = extent;
    if (config.framebufferSize.width > 0 && config.framebufferSize.height > 0) {
        actualExtent = config.framebufferSize;
    }

    // Create color images and views
    colorImages.reserve(config.colorAttachments.size());
    colorImageViews.reserve(config.colorAttachments.size());
    for (size_t i = 0; i < config.colorAttachments.size(); ++i) {
        const auto& colorAttach = config.colorAttachments[i];

        // Create color image
        ImageInfo imageInfo{};
        imageInfo.width = actualExtent.width;
        imageInfo.height = actualExtent.height;
        imageInfo.format = colorAttach.format;
        imageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
        imageInfo.memoryUsage = MemoryUsage::GPU_ONLY;
        imageInfo.debugName = config.name + "_Color";

        auto colorImage = ResourceFactory::createImage(context, imageInfo);
        auto colorImageView = ResourceFactory::createImageView(context, colorImage, vk::ImageViewType::e2D, vk::ImageAspectFlagBits::eColor);

        colorImages.push_back(colorImage);
        colorImageViews.push_back(colorImageView);
    }

    // Create depth image if needed
    if (config.hasDepth) {
        ImageInfo depthImageInfo{};
        depthImageInfo.width = actualExtent.width;
        depthImageInfo.height = actualExtent.height;
        depthImageInfo.format = config.depthAttachment.format;
        depthImageInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
        depthImageInfo.memoryUsage = MemoryUsage::GPU_ONLY;
        depthImageInfo.debugName = config.name + "_Depth";

        depthImage = ResourceFactory::createImage(context, depthImageInfo);
        depthImageView = ResourceFactory::createImageView(context, depthImage, vk::ImageViewType::e2D, vk::ImageAspectFlagBits::eDepth);
    }

    // Create framebuffers (currently just one, can be extended for multiple frames)
    framebuffers.resize(1);

    eastl::vector<vk::ImageView> attachments;
    for (const auto& colorImageView : colorImageViews) {
        attachments.push_back(colorImageView);
    }
    if (depthImageView != VK_NULL_HANDLE) {
        attachments.push_back(depthImageView);
    }

    vk::FramebufferCreateInfo framebufferInfo{};
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = actualExtent.width;
    framebufferInfo.height = actualExtent.height;
    framebufferInfo.layers = 1;

    framebuffers[0] = context->getDevice().createFramebuffer(framebufferInfo);
}

void RenderPass::recreateFramebuffers(vk::Extent2D newExtent) {
    if (!config.createOwnFramebuffer) {
        return;
    }

    cleanupFramebuffers();
    createFramebuffers(newExtent);
}

void RenderPass::cleanupFramebuffers() {
    // Cleanup framebuffers
    for (auto& framebuffer : framebuffers) {
        if (framebuffer) {
            context->getDevice().destroyFramebuffer(framebuffer);
        }
    }
    framebuffers.clear();

    // Cleanup image views
    for (auto& imageView : colorImageViews) {
        if (imageView) {
            context->getDevice().destroyImageView(imageView);
        }
    }
    colorImageViews.clear();

    if (depthImageView != VK_NULL_HANDLE) {
        context->getDevice().destroyImageView(depthImageView);
        depthImageView = VK_NULL_HANDLE;
    }

    // Cleanup images
    for (auto& image : colorImages) {
        ResourceFactory::destroyImage(context, image);
    }
    colorImages.clear();

    if (depthImage.image != VK_NULL_HANDLE) {
        ResourceFactory::destroyImage(context, depthImage);
        depthImage = {};
    }

    currentExtent.width = 0;
    currentExtent.height = 0;
}

vk::Framebuffer RenderPass::getFramebuffer(uint32_t frameIndex) const {
    if (!config.createOwnFramebuffer || framebuffers.empty()) {
        return VK_NULL_HANDLE;
    }

    // For now, we only have one framebuffer, but this can be extended
    return framebuffers[0];
}

}