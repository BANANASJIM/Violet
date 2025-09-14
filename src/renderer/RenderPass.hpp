#pragma once

#include <vulkan/vulkan.hpp>

namespace violet {

class VulkanContext;

class RenderPass {
public:
    void init(VulkanContext* context, vk::Format colorFormat);
    void cleanup();

    vk::RenderPass getRenderPass() const { return renderPass; }

private:
    VulkanContext* context = nullptr;
    vk::RenderPass renderPass;
};

}