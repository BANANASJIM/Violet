#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>

namespace violet {

class VulkanContext;

class RenderPassBuilder {
public:
    RenderPassBuilder& addColorAttachment(vk::Format format,
                                         vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eClear,
                                         vk::AttachmentStoreOp storeOp = vk::AttachmentStoreOp::eStore,
                                         vk::ImageLayout finalLayout = vk::ImageLayout::ePresentSrcKHR);

    RenderPassBuilder& addDepthAttachment(vk::Format format,
                                         vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eClear,
                                         vk::AttachmentStoreOp storeOp = vk::AttachmentStoreOp::eDontCare);

    RenderPassBuilder& addSubpassDependency(uint32_t srcSubpass, uint32_t dstSubpass,
                                           vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage,
                                           vk::AccessFlags srcAccess, vk::AccessFlags dstAccess);

    vk::RenderPass build(VulkanContext* context);

private:
    eastl::vector<vk::AttachmentDescription> attachments;
    eastl::vector<vk::SubpassDependency> dependencies;
    eastl::vector<uint32_t> colorAttachmentIndices;
    uint32_t depthAttachmentIndex = VK_ATTACHMENT_UNUSED;
};

}