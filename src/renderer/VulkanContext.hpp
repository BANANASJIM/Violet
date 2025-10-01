#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <GLFW/glfw3.h>
#include <EASTL/vector.h>
#include <EASTL/optional.h>
#include <EASTL/string.h>

#include "RenderSettings.hpp"

namespace violet {

struct QueueFamilyIndices {
    eastl::optional<uint32_t> graphicsFamily;
    eastl::optional<uint32_t> presentFamily;
    eastl::optional<uint32_t> computeFamily;
    eastl::optional<uint32_t> transferFamily;
    
    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapchainSupportDetails {
    vk::SurfaceCapabilitiesKHR capabilities;
    eastl::vector<vk::SurfaceFormatKHR> formats;
    eastl::vector<vk::PresentModeKHR> presentModes;
};

class VulkanContext {
public:
    void init(GLFWwindow* window);
    void cleanup();

    vk::Instance getInstance() const { return *instance; }
    vk::Device getDevice() const { return *device; }
    vk::PhysicalDevice getPhysicalDevice() const { return *physicalDevice; }
    vk::Queue getGraphicsQueue() const { return *graphicsQueue; }
    vk::Queue getPresentQueue() const { return *presentQueue; }
    vk::SurfaceKHR getSurface() const { return *surface; }
    vk::CommandPool getCommandPool() const { return *commandPool; }
    VmaAllocator getAllocator() const { return allocator; }

    // RAII object accessors for creating child objects
    const vk::raii::Device& getDeviceRAII() const { return device; }
    const vk::raii::PhysicalDevice& getPhysicalDeviceRAII() const { return physicalDevice; }

    QueueFamilyIndices getQueueFamilies() const { return queueFamilies; }
    SwapchainSupportDetails querySwapchainSupport() const;
    vk::Format findDepthFormat();
    GLFWwindow* getWindow() const { return window; }

    const RenderSettings& getRenderSettings() const { return renderSettings; }

private:
    void createInstance();
    void setupDebugMessenger();
    void createSurface(GLFWwindow* window);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createCommandPool();
    void createAllocator();
    
    bool isDeviceSuitable(vk::PhysicalDevice device);
    QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice device);
    bool checkDeviceExtensionSupport(vk::PhysicalDevice device);
    vk::Format findSupportedFormat(const eastl::vector<vk::Format>& candidates,
                                   vk::ImageTiling tiling, vk::FormatFeatureFlags features);
    
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);

private:
    vk::raii::Context context;
    vk::raii::Instance instance{nullptr};
    vk::raii::DebugUtilsMessengerEXT debugMessenger{nullptr};
    vk::raii::PhysicalDevice physicalDevice{nullptr};
    vk::raii::Device device{nullptr};
    vk::raii::SurfaceKHR surface{nullptr};

    vk::raii::Queue graphicsQueue{nullptr};
    vk::raii::Queue presentQueue{nullptr};
    vk::raii::Queue computeQueue{nullptr};
    vk::raii::Queue transferQueue{nullptr};

    vk::raii::CommandPool commandPool{nullptr};
    VmaAllocator allocator = VK_NULL_HANDLE;

    QueueFamilyIndices queueFamilies;
    GLFWwindow* window;
    RenderSettings renderSettings;

    const eastl::vector<const char*> validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };
    
    const eastl::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
#ifdef __APPLE__
        ,"VK_KHR_portability_subset"
#endif
    };
    
#ifdef VIOLET_DEBUG
    const bool enableValidationLayers = true;
#else
    const bool enableValidationLayers = false;
#endif
};

}