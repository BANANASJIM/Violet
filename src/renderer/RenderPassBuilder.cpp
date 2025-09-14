#include "RenderPassBuilder.hpp"
#include "VulkanContext.hpp"

namespace violet {

RenderPassBuilder& RenderPassBuilder::addColorAttachment(vk::Format format,
                                                        vk::AttachmentLoadOp loadOp,
                                                        vk::AttachmentStoreOp storeOp,
                                                        vk::ImageLayout finalLayout) {
    vk::AttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = vk::SampleCountFlagBits::e1;
    attachment.loadOp = loadOp;
    attachment.storeOp = storeOp;
    attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    attachment.initialLayout = vk::ImageLayout::eUndefined;
    attachment.finalLayout = finalLayout;

    colorAttachmentIndices.push_back(static_cast<uint32_t>(attachments.size()));
    attachments.push_back(attachment);
    return *this;
}

RenderPassBuilder& RenderPassBuilder::addDepthAttachment(vk::Format format,
                                                        vk::AttachmentLoadOp loadOp,
                                                        vk::AttachmentStoreOp storeOp) {
    vk::AttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = vk::SampleCountFlagBits::e1;
    attachment.loadOp = loadOp;
    attachment.storeOp = storeOp;
    attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    attachment.initialLayout = vk::ImageLayout::eUndefined;
    attachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    depthAttachmentIndex = static_cast<uint32_t>(attachments.size());
    attachments.push_back(attachment);
    return *this;
}

RenderPassBuilder& RenderPassBuilder::addSubpassDependency(uint32_t srcSubpass, uint32_t dstSubpass,
                                                          vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage,
                                                          vk::AccessFlags srcAccess, vk::AccessFlags dstAccess) {
    vk::SubpassDependency dependency{};
    dependency.srcSubpass = srcSubpass;
    dependency.dstSubpass = dstSubpass;
    dependency.srcStageMask = srcStage;
    dependency.dstStageMask = dstStage;
    dependency.srcAccessMask = srcAccess;
    dependency.dstAccessMask = dstAccess;

    dependencies.push_back(dependency);
    return *this;
}

vk::RenderPass RenderPassBuilder::build(VulkanContext* context) {
    eastl::vector<vk::AttachmentReference> colorRefs;
    for (uint32_t index : colorAttachmentIndices) {
        vk::AttachmentReference ref{};
        ref.attachment = index;
        ref.layout = vk::ImageLayout::eColorAttachmentOptimal;
        colorRefs.push_back(ref);
    }

    vk::AttachmentReference depthRef{};
    vk::AttachmentReference* pDepthRef = nullptr;
    if (depthAttachmentIndex != VK_ATTACHMENT_UNUSED) {
        depthRef.attachment = depthAttachmentIndex;
        depthRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        pDepthRef = &depthRef;
    }

    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
    subpass.pColorAttachments = colorRefs.data();
    subpass.pDepthStencilAttachment = pDepthRef;

    vk::RenderPassCreateInfo createInfo{};
    createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    createInfo.pAttachments = attachments.data();
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    createInfo.pDependencies = dependencies.data();

    return context->getDevice().createRenderPass(createInfo);
}

}