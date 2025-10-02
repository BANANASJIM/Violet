#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>
#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <functional>
#include "ResourceFactory.hpp"
#include "Pass.hpp"

namespace violet {

class VulkanContext;

// Attachment abstraction for reusability
struct AttachmentDesc {
    vk::Format format;
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
    vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eClear;
    vk::AttachmentStoreOp storeOp = vk::AttachmentStoreOp::eStore;
    vk::ImageLayout initialLayout = vk::ImageLayout::eUndefined;
    vk::ImageLayout finalLayout;

    // Factory methods for common attachment types
    static AttachmentDesc color(vk::Format fmt, vk::AttachmentLoadOp load = vk::AttachmentLoadOp::eClear);
    static AttachmentDesc depth(vk::Format fmt, vk::AttachmentLoadOp load = vk::AttachmentLoadOp::eClear,
                                vk::AttachmentStoreOp store = vk::AttachmentStoreOp::eDontCare);
    static AttachmentDesc swapchainColor(vk::Format fmt, vk::AttachmentLoadOp load = vk::AttachmentLoadOp::eClear);
    static AttachmentDesc swapchainDepth(vk::Format fmt, vk::AttachmentLoadOp load = vk::AttachmentLoadOp::eClear);

    // Convert to Vulkan attachment description
    vk::AttachmentDescription toVulkan() const;
};

// Unified render pass configuration
struct RenderPassConfig : public PassConfigBase {
    // Note: name, type, srcStage, dstStage, srcAccess, dstAccess, execute are in PassConfigBase

    // Attachments using abstraction
    eastl::vector<AttachmentDesc> colorAttachments;
    AttachmentDesc depthAttachment{};
    bool hasDepth = false;

    // Clear values
    eastl::vector<vk::ClearValue> clearValues;

    // Framebuffer management options
    bool isSwapchainPass = false;        // Whether this uses external swapchain framebuffer
    bool createOwnFramebuffer = true;    // Whether to create own framebuffer (default true)
    vk::Extent2D framebufferSize = {0, 0}; // 0 means use swapchain size
    bool followsSwapchainSize = true;    // Whether size depends on swapchain

    // Default constructor
    RenderPassConfig() {
        type = PassType::Graphics;
        srcStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dstStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    }
};

class RenderPass : public Pass {
public:
    void init(VulkanContext* context, const RenderPassConfig& config);
    void cleanup() override;

    // Framebuffer management
    void createFramebuffers(vk::Extent2D extent);
    void recreateFramebuffers(vk::Extent2D newExtent);
    void cleanupFramebuffers();

    // Pass interface implementation
    void begin(vk::CommandBuffer cmd, uint32_t frameIndex) override;
    void execute(vk::CommandBuffer cmd, uint32_t frameIndex) override;
    void end(vk::CommandBuffer cmd) override;

    PassType getType() const override { return PassType::Graphics; }
    const eastl::string& getName() const override { return config.name; }

    // Barrier configuration access
    vk::PipelineStageFlags getSrcStage() const override { return config.srcStage; }
    vk::PipelineStageFlags getDstStage() const override { return config.dstStage; }
    vk::AccessFlags getSrcAccess() const override { return config.srcAccess; }
    vk::AccessFlags getDstAccess() const override { return config.dstAccess; }

    // Overloaded pass execution (for backwards compatibility and flexibility)
    void begin(vk::CommandBuffer cmd, vk::Framebuffer framebuffer, vk::Extent2D extent);
    void begin(vk::CommandBuffer cmd, vk::Extent2D extent); // Use own framebuffer

    // External framebuffer support (for swapchain passes)
    void setExternalFramebuffer(vk::Framebuffer framebuffer);
    void onSwapchainRecreate(vk::Extent2D newSize);

    vk::RenderPass getRenderPass() const { return renderPass; }
    const RenderPassConfig& getConfig() const { return config; }

    // Access to own framebuffers
    vk::Framebuffer getFramebuffer(uint32_t frameIndex = 0) const;

    // Access to attachment image views (for use as shader inputs)
    vk::ImageView getColorImageView(uint32_t index = 0) const;
    vk::ImageView getDepthImageView() const;
    vk::Image getColorImage(uint32_t index = 0) const;
    vk::Image getDepthImage() const;

    // Static helper for explicit barrier insertion between passes
    static void insertImageBarrier(
        vk::CommandBuffer cmd,
        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout,
        vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eColorAttachmentOutput,
        vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eColorAttachmentOutput,
        vk::AccessFlags srcAccess = {},
        vk::AccessFlags dstAccess = {}
    );

    // Helper for memory barriers
    static void insertMemoryBarrier(
        vk::CommandBuffer cmd,
        vk::PipelineStageFlags srcStage,
        vk::PipelineStageFlags dstStage,
        vk::AccessFlags srcAccess,
        vk::AccessFlags dstAccess
    );

private:
    VulkanContext* context = nullptr;
    vk::RenderPass renderPass;
    RenderPassConfig config;

    // External framebuffer support (for swapchain passes)
    vk::Framebuffer externalFramebuffer = VK_NULL_HANDLE;

    // Own framebuffer resources
    eastl::vector<ImageResource> colorImages;
    eastl::vector<vk::ImageView> colorImageViews;
    ImageResource depthImage{};
    vk::ImageView depthImageView = VK_NULL_HANDLE;
    eastl::vector<vk::Framebuffer> framebuffers;  // Support for multiple frames in flight
    vk::Extent2D currentExtent = {0, 0};
};

}