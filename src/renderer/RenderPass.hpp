#pragma once

#include <vulkan/vulkan.hpp>

namespace violet {

class VulkanContext;
class RenderPassBuilder;

class RenderPass {
public:
    void init(VulkanContext* context, vk::Format colorFormat);
    void init(VulkanContext* context, RenderPassBuilder& builder);
    void cleanup();

    vk::RenderPass getRenderPass() const { return renderPass; }

private:
    VulkanContext* context = nullptr;
    vk::RenderPass renderPass;
};

}