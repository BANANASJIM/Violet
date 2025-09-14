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
    
    vk::SwapchainKHR getSwapchain() const { return swapchain; }
    vk::Format getImageFormat() const { return imageFormat; }
    vk::Extent2D getExtent() const { return extent; }
    const eastl::vector<vk::ImageView>& getImageViews() const { return imageViews; }
    
    uint32_t acquireNextImage(vk::Semaphore semaphore);
    void present(uint32_t imageIndex, vk::Semaphore waitSemaphore);

    void createFramebuffers(vk::RenderPass renderPass);
    const eastl::vector<vk::Framebuffer>& getFramebuffers() const { return framebuffers; }

    void createDepthResources();

private:
    void create();
    void createImageViews();
    
    vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const eastl::vector<vk::SurfaceFormatKHR>& availableFormats);
    vk::PresentModeKHR chooseSwapPresentMode(const eastl::vector<vk::PresentModeKHR>& availablePresentModes);
    vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities);

private:
    VulkanContext* context;
    
    vk::SwapchainKHR swapchain = nullptr;
    eastl::vector<vk::Image> images;
    eastl::vector<vk::ImageView> imageViews;
    eastl::vector<vk::Framebuffer> framebuffers;

    vk::Image depthImage = nullptr;
    vk::DeviceMemory depthImageMemory = nullptr;
    vk::ImageView depthImageView = nullptr;

    vk::Format imageFormat;
    vk::Extent2D extent;
};

}