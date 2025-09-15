#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/unique_ptr.h>

struct GLFWwindow;

namespace violet {

class VulkanContext;

class ImGuiVulkanBackend {
public:
    ImGuiVulkanBackend() = default;
    ~ImGuiVulkanBackend();

    void init(VulkanContext* context, GLFWwindow* window, vk::RenderPass renderPass, uint32_t imageCount);
    void cleanup();

    void uploadFonts();

private:
    void createDescriptorPool();

private:
    VulkanContext* context{nullptr};
    vk::DescriptorPool descriptorPool;
    bool initialized{false};
};

}
