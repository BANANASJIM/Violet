#pragma once

#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>
#include <EASTL/vector.h>
#include <EASTL/optional.h>
#include <EASTL/string.h>

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
    
    vk::Instance getInstance() const { return instance; }
    vk::Device getDevice() const { return device; }
    vk::PhysicalDevice getPhysicalDevice() const { return physicalDevice; }
    vk::Queue getGraphicsQueue() const { return graphicsQueue; }
    vk::Queue getPresentQueue() const { return presentQueue; }
    vk::SurfaceKHR getSurface() const { return surface; }
    
    QueueFamilyIndices getQueueFamilies() const { return queueFamilies; }
    SwapchainSupportDetails querySwapchainSupport() const;

private:
    void createInstance();
    void setupDebugMessenger();
    void createSurface(GLFWwindow* window);
    void pickPhysicalDevice();
    void createLogicalDevice();
    
    bool isDeviceSuitable(vk::PhysicalDevice device);
    QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice device);
    bool checkDeviceExtensionSupport(vk::PhysicalDevice device);
    
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);

private:
    vk::Instance instance;
    vk::DebugUtilsMessengerEXT debugMessenger;
    vk::PhysicalDevice physicalDevice;
    vk::Device device;
    vk::SurfaceKHR surface;
    
    vk::Queue graphicsQueue;
    vk::Queue presentQueue;
    vk::Queue computeQueue;
    vk::Queue transferQueue;
    
    QueueFamilyIndices queueFamilies;
    
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