#include "Swapchain.hpp"
#include "VulkanContext.hpp"
#include "Buffer.hpp"
#include "core/Log.hpp"
#include <GLFW/glfw3.h>
#include <EASTL/array.h>
#include <algorithm>
#include <limits>

namespace violet {

void Swapchain::init(VulkanContext* ctx) {
    context = ctx;
    create();
    createImageViews();
    createDepthResources();
}

void Swapchain::cleanup() {
    auto device = context->getDevice();

    for (auto framebuffer : framebuffers) {
        device.destroyFramebuffer(framebuffer);
    }
    framebuffers.clear();

    if (depthImageView) {
        VT_TRACE("Destroying depth image view");
        device.destroyImageView(depthImageView);
        depthImageView = nullptr;
    }
    if (depthImage) {
        VT_TRACE("Destroying depth image");
        device.destroyImage(depthImage);
        depthImage = nullptr;
    }
    if (depthImageMemory) {
        VT_TRACE("Freeing depth image memory");
        device.freeMemory(depthImageMemory);
        depthImageMemory = nullptr;
    }

    for (auto imageView : imageViews) {
        device.destroyImageView(imageView);
    }

    device.destroySwapchainKHR(swapchain);
}

void Swapchain::recreate() {
    cleanup();
    create();
    createImageViews();
    createDepthResources();
}

void Swapchain::create() {
    auto supportDetails = context->querySwapchainSupport();
    
    auto surfaceFormat = chooseSwapSurfaceFormat(supportDetails.formats);
    auto presentMode = chooseSwapPresentMode(supportDetails.presentModes);
    extent = chooseSwapExtent(supportDetails.capabilities);
    
    uint32_t imageCount = supportDetails.capabilities.minImageCount + 1;
    if (supportDetails.capabilities.maxImageCount > 0 && 
        imageCount > supportDetails.capabilities.maxImageCount) {
        imageCount = supportDetails.capabilities.maxImageCount;
    }
    
    vk::SwapchainCreateInfoKHR createInfo;
    createInfo.surface = context->getSurface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
    createInfo.preTransform = supportDetails.capabilities.currentTransform;
    createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    
    auto indices = context->getQueueFamilies();
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};
    
    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = vk::SharingMode::eExclusive;
    }
    
    swapchain = context->getDevice().createSwapchainKHR(createInfo);
    auto stdImages = context->getDevice().getSwapchainImagesKHR(swapchain);
    images.clear();
    for (const auto& img : stdImages) {
        images.push_back(img);
    }
    imageFormat = surfaceFormat.format;
}

void Swapchain::createImageViews() {
    imageViews.resize(images.size());
    
    for (size_t i = 0; i < images.size(); i++) {
        vk::ImageViewCreateInfo createInfo;
        createInfo.image = images[i];
        createInfo.viewType = vk::ImageViewType::e2D;
        createInfo.format = imageFormat;
        createInfo.components.r = vk::ComponentSwizzle::eIdentity;
        createInfo.components.g = vk::ComponentSwizzle::eIdentity;
        createInfo.components.b = vk::ComponentSwizzle::eIdentity;
        createInfo.components.a = vk::ComponentSwizzle::eIdentity;
        createInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;
        
        imageViews[i] = context->getDevice().createImageView(createInfo);
    }
}

uint32_t Swapchain::acquireNextImage(vk::Semaphore semaphore) {
    uint32_t imageIndex;
    context->getDevice().acquireNextImageKHR(swapchain, UINT64_MAX, semaphore, {}, &imageIndex);
    return imageIndex;
}

void Swapchain::present(uint32_t imageIndex, vk::Semaphore waitSemaphore) {
    vk::PresentInfoKHR presentInfo;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &waitSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;
    
    context->getPresentQueue().presentKHR(presentInfo);
}

vk::SurfaceFormatKHR Swapchain::chooseSwapSurfaceFormat(const eastl::vector<vk::SurfaceFormatKHR>& availableFormats) {
    for (const auto& format : availableFormats) {
        if (format.format == vk::Format::eB8G8R8A8Srgb && 
            format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            return format;
        }
    }
    return availableFormats[0];
}

vk::PresentModeKHR Swapchain::chooseSwapPresentMode(const eastl::vector<vk::PresentModeKHR>& availablePresentModes) {
    for (const auto& mode : availablePresentModes) {
        if (mode == vk::PresentModeKHR::eMailbox) {
            return mode;
        }
    }
    return vk::PresentModeKHR::eFifo;
}

vk::Extent2D Swapchain::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    
    int width, height;
    glfwGetFramebufferSize(context->getWindow(), &width, &height);
    
    vk::Extent2D actualExtent = {
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
    };
    
    actualExtent.width = std::clamp(actualExtent.width, 
                                    capabilities.minImageExtent.width, 
                                    capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height, 
                                     capabilities.minImageExtent.height, 
                                     capabilities.maxImageExtent.height);
    
    return actualExtent;
}

void Swapchain::createFramebuffers(vk::RenderPass renderPass) {
    framebuffers.resize(imageViews.size());

    for (size_t i = 0; i < imageViews.size(); i++) {
        eastl::array<vk::ImageView, 2> attachments = {
            imageViews[i],
            depthImageView
        };

        vk::FramebufferCreateInfo framebufferInfo;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;

        framebuffers[i] = context->getDevice().createFramebuffer(framebufferInfo);
    }
}

void Swapchain::createDepthResources() {
    vk::Format depthFormat = context->findDepthFormat();

    // Create depth image
    vk::ImageCreateInfo imageInfo;
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent.width = extent.width;
    imageInfo.extent.height = extent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;

    depthImage = context->getDevice().createImage(imageInfo);

    vk::MemoryRequirements memRequirements = context->getDevice().getImageMemoryRequirements(depthImage);

    vk::MemoryAllocateInfo allocInfo;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(context, memRequirements.memoryTypeBits,
                                               vk::MemoryPropertyFlagBits::eDeviceLocal);

    depthImageMemory = context->getDevice().allocateMemory(allocInfo);

    context->getDevice().bindImageMemory(depthImage, depthImageMemory, 0);

    // Create depth image view
    vk::ImageViewCreateInfo viewInfo;
    viewInfo.image = depthImage;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    depthImageView = context->getDevice().createImageView(viewInfo);
}

}