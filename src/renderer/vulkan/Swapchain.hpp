#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>
#include "resource/gpu/ResourceFactory.hpp"

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

    // Dynamic rendering accessors (no framebuffers needed)
    const eastl::vector<vk::raii::ImageView>& getImageViews() const { return imageViews; }
    vk::ImageView getImageView(size_t index) const { return *imageViews[index]; }
    vk::ImageView getDepthImageView() const { return *depthImageView; }
    size_t getImageCount() const { return images.size(); }

    // RenderGraph integration - expose vk::Image handles
    vk::Image getImage(size_t index) const { return images[index]; }
    vk::Image getDepthImage() const { return depthImage.image; }

    // RenderGraph integration - get ImageResource wrappers (unified API)
    const ImageResource* getImageResource(size_t index) const;
    const ImageResource* getDepthImageResource() const;

    void createDepthResources();

private:
    void createResources();
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
    eastl::vector<ImageResource> imageResources;  // Persistent wrappers for RenderGraph

    ImageResource depthImage{};
    vk::raii::ImageView depthImageView{nullptr};

    vk::Format imageFormat;
    vk::Extent2D extent;
};

}