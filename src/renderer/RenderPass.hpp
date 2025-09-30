#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>
#include <EASTL/string.h>
#include <functional>

namespace violet {

class VulkanContext;
class RenderPassBuilder;

// Declarative render pass descriptor for easier configuration
struct RenderPassDesc {
    eastl::string name;

    // Attachment descriptions
    eastl::vector<vk::AttachmentDescription> colorAttachments;
    vk::AttachmentDescription depthAttachment{};
    bool hasDepth = false;

    // Clear values
    eastl::vector<vk::ClearValue> clearValues;

    // Execution callback
    eastl::function<void(vk::CommandBuffer, uint32_t)> execute;

    // Layout hints for transitions between passes
    vk::ImageLayout finalColorLayout = vk::ImageLayout::eColorAttachmentOptimal;
    vk::ImageLayout finalDepthLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
};

// Pass configuration structure (legacy, for compatibility)
struct PassConfig {
    eastl::string name;

    // Attachments
    eastl::vector<vk::AttachmentDescription> colorAttachments;
    eastl::vector<vk::ClearValue> clearValues;
    bool hasDepth = false;
    vk::AttachmentDescription depthAttachment{};

    // Execution callback
    eastl::function<void(vk::CommandBuffer, uint32_t)> execute;

    // Pipeline barriers (optional)
    vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::AccessFlags srcAccess = vk::AccessFlagBits::eColorAttachmentWrite;
    vk::AccessFlags dstAccess = vk::AccessFlagBits::eColorAttachmentRead;
};

class RenderPass {
public:
    void init(VulkanContext* context, vk::Format colorFormat);
    void init(VulkanContext* context, RenderPassBuilder& builder);
    void init(VulkanContext* context, const PassConfig& config);
    void init(VulkanContext* context, const RenderPassDesc& desc);
    void cleanup();

    // Pass execution
    void begin(vk::CommandBuffer cmd, vk::Framebuffer framebuffer, vk::Extent2D extent);
    void execute(vk::CommandBuffer cmd, uint32_t frameIndex);
    void end(vk::CommandBuffer cmd);

    vk::RenderPass getRenderPass() const { return renderPass; }
    const PassConfig& getConfig() const { return config; }

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
    PassConfig config;
};

}