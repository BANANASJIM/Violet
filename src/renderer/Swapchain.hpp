#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>

namespace violet {

class VulkanContext;

class Swapchain {
public:
    void init(VulkanContext* context);
    void cleanup();
    void recreate();
    
    vk::SwapchainKHR getSwapchain() const { return *swapchain; }
    vk::Format getImageFormat() const { return imageFormat; }
    vk::Extent2D getExtent() const { return extent; }
    
    uint32_t acquireNextImage(vk::Semaphore semaphore);
    void present(uint32_t imageIndex, vk::Semaphore waitSemaphore);

    void createFramebuffers(vk::RenderPass renderPass);
    vk::Framebuffer getFramebuffer(size_t index) const { return *framebuffers[index]; }
    size_t getFramebufferCount() const { return framebuffers.size(); }

    void createDepthResources();

private:
    void create();
    void createImageViews();
    
    vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const eastl::vector<vk::SurfaceFormatKHR>& availableFormats);
    vk::PresentModeKHR chooseSwapPresentMode(const eastl::vector<vk::PresentModeKHR>& availablePresentModes);
    vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities);

private:
    VulkanContext* context;
    
    vk::raii::SwapchainKHR swapchain{nullptr};
    eastl::vector<vk::Image> images;
    eastl::vector<vk::raii::ImageView> imageViews;
    eastl::vector<vk::raii::Framebuffer> framebuffers;

    vk::raii::Image depthImage{nullptr};
    vk::raii::DeviceMemory depthImageMemory{nullptr};
    vk::raii::ImageView depthImageView{nullptr};

    vk::Format imageFormat;
    vk::Extent2D extent;
};

}