#include "Swapchain.hpp"
#include "renderer/core/VulkanContext.hpp"
#include "resource/gpu/ResourceFactory.hpp"
#include "core/Log.hpp"
#include <GLFW/glfw3.h>
#include <EASTL/array.h>
#include <algorithm>
#include <limits>

namespace violet {

void Swapchain::init(VulkanContext* ctx) {
    context = ctx;
    createResources();
}

void Swapchain::cleanup() {
    // RAII objects will automatically clean themselves up
    // Reset in reverse order of creation
    framebuffers.clear();

    // Destroy depth resources in correct order
    if (depthImageView != nullptr) {
        depthImageView = nullptr;
    }

    if (depthImage.image != VK_NULL_HANDLE) {
        ResourceFactory::destroyImage(context, depthImage);
        depthImage = {};
    }

    imageViews.clear();
    swapchain = nullptr;
}

void Swapchain::recreate() {
    cleanup();
    createResources();
}

void Swapchain::createResources() {
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
    
    auto indices = context->getQueueFamilies();
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    vk::SwapchainCreateInfoKHR createInfo(
        {}, // flags
        context->getSurface(),
        imageCount,
        surfaceFormat.format,
        surfaceFormat.colorSpace,
        extent,
        1, // imageArrayLayers
        vk::ImageUsageFlagBits::eColorAttachment,
        (indices.graphicsFamily != indices.presentFamily) ?
            vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive,
        (indices.graphicsFamily != indices.presentFamily) ? 2u : 0u,
        (indices.graphicsFamily != indices.presentFamily) ? queueFamilyIndices : nullptr,
        supportDetails.capabilities.currentTransform,
        vk::CompositeAlphaFlagBitsKHR::eOpaque,
        presentMode,
        VK_TRUE
    );
    
    swapchain = vk::raii::SwapchainKHR(context->getDeviceRAII(), createInfo);
    auto stdImages = swapchain.getImages();
    images.clear();
    for (const auto& img : stdImages) {
        images.push_back(img);
    }
    imageFormat = surfaceFormat.format;
}

void Swapchain::createImageViews() {
    imageViews.clear();
    imageViews.reserve(images.size());

    for (size_t i = 0; i < images.size(); i++) {
        vk::ImageViewCreateInfo createInfo(
            {}, // flags
            images[i],
            vk::ImageViewType::e2D,
            imageFormat,
            vk::ComponentMapping{},
            vk::ImageSubresourceRange(
                vk::ImageAspectFlagBits::eColor,
                0, // baseMipLevel
                1, // levelCount
                0, // baseArrayLayer
                1  // layerCount
            )
        );

        imageViews.emplace_back(context->getDeviceRAII(), createInfo);
    }
}

uint32_t Swapchain::acquireNextImage(vk::Semaphore semaphore) {
    try {
        auto result = swapchain.acquireNextImage(UINT64_MAX, semaphore);
        return result.second;
    } catch (const vk::OutOfDateKHRError&) {
        // Swapchain is out of date, caller should handle recreation
        throw;
    } catch (const vk::SystemError& e) {
        // Handle other Vulkan errors including SuboptimalKHR
        if (e.code() == vk::Result::eSuboptimalKHR) {
            // Swapchain is suboptimal but still usable, caller may want to recreate
            throw;
        }
        throw;
    }
}

void Swapchain::present(uint32_t imageIndex, vk::Semaphore waitSemaphore) {
    vk::SwapchainKHR swapchainHandle = *swapchain;
    vk::PresentInfoKHR presentInfo(
        1, // waitSemaphoreCount
        &waitSemaphore,
        1, // swapchainCount
        &swapchainHandle,
        &imageIndex
    );

    auto presentResult = context->getPresentQueue().presentKHR(presentInfo);
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
    framebuffers.clear();
    framebuffers.reserve(imageViews.size());

    for (size_t i = 0; i < imageViews.size(); i++) {
        eastl::array<vk::ImageView, 2> attachments = {
            *imageViews[i],
            *depthImageView
        };

        vk::FramebufferCreateInfo framebufferInfo(
            {}, // flags
            renderPass,
            static_cast<uint32_t>(attachments.size()),
            attachments.data(),
            extent.width,
            extent.height,
            1 // layers
        );

        framebuffers.emplace_back(context->getDeviceRAII(), framebufferInfo);
    }
}

void Swapchain::createDepthResources() {
    vk::Format depthFormat = context->findDepthFormat();

    // Create depth image using ResourceFactory for consistent resource management
    ImageInfo depthImageInfo{
        .width = extent.width,
        .height = extent.height,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .format = depthFormat,
        .imageType = vk::ImageType::e2D,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
        .flags = {},
        .samples = vk::SampleCountFlagBits::e1,
        .memoryUsage = MemoryUsage::GPU_ONLY,
        .debugName = "Swapchain Depth Buffer"
    };

    depthImage = ResourceFactory::createImage(context, depthImageInfo);

    // Create depth image view using constructor initialization
    vk::ImageViewCreateInfo viewInfo(
        {}, // flags
        depthImage.image,
        vk::ImageViewType::e2D,
        depthFormat,
        vk::ComponentMapping{},
        vk::ImageSubresourceRange(
            vk::ImageAspectFlagBits::eDepth,
            0, // baseMipLevel
            1, // levelCount
            0, // baseArrayLayer
            1  // layerCount
        )
    );

    depthImageView = vk::raii::ImageView(context->getDeviceRAII(), viewInfo);
}

}