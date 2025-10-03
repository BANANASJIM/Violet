#include "core/Exception.hpp"
#include "renderer/core/VulkanContext.hpp"
#include "core/Log.hpp"
#include <EASTL/set.h>
#include <EASTL/algorithm.h>

namespace violet {

void VulkanContext::init(GLFWwindow* win) {
    window = win;
    createInstance();
    setupDebugMessenger();
    createSurface(window);
    pickPhysicalDevice();

    // Load render settings from config file
    renderSettings = RenderSettings::loadFromFile("config.json", *physicalDevice);

    createLogicalDevice();
    createCommandPool();
    createAllocator();
}

void VulkanContext::cleanup() {
    // Wait for device to be idle before cleanup
    if (*device) {
        device.waitIdle();
    }

    // Clean up VMA allocator before destroying the device
    if (allocator != VK_NULL_HANDLE) {
#ifdef _DEBUG
        // Check for memory leaks before destroying allocator
        VmaTotalStatistics stats;
        vmaCalculateStatistics(allocator, &stats);
        if (stats.total.statistics.allocationCount > 0) {
            violet::Log::error("VMA", "Memory leak detected: {} allocations still active", stats.total.statistics.allocationCount);
            // Dump detailed stats only when leaks are detected
            char* statsString = nullptr;
            vmaBuildStatsString(allocator, &statsString, VK_TRUE);
            if (statsString) {
                violet::Log::error("VMA", "Allocator stats (leak detected):\n{}", statsString);
                vmaFreeStatsString(allocator, statsString);
            }
        }
#endif
        vmaDestroyAllocator(allocator);
        allocator = VK_NULL_HANDLE;
    }

    // RAII objects will automatically clean themselves up
    // Reset in reverse order of creation to ensure proper cleanup
    commandPool = nullptr;
    transferQueue = nullptr;
    computeQueue = nullptr;
    presentQueue = nullptr;
    graphicsQueue = nullptr;
    device = nullptr;
    surface = nullptr;
    debugMessenger = nullptr;
    physicalDevice = nullptr;
    instance = nullptr;
}

void VulkanContext::createInstance() {
#ifdef __APPLE__
    // Enable MoltenVK Metal argument buffers for bindless support
    // This is required for descriptor indexing on macOS
    setenv("MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS", "1", 1);
    violet::Log::info("Renderer", "Enabled MoltenVK Metal argument buffers for bindless support");
#endif

    vk::ApplicationInfo appInfo;
    appInfo.pApplicationName = "Violet Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Violet";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    eastl::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

#ifdef __APPLE__
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    extensions.push_back("VK_KHR_get_physical_device_properties2");
#endif

    vk::InstanceCreateInfo createInfo;
#ifdef __APPLE__
    createInfo.flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
#endif
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledLayerCount = enableValidationLayers ? static_cast<uint32_t>(validationLayers.size()) : 0;
    createInfo.ppEnabledLayerNames = enableValidationLayers ? validationLayers.data() : nullptr;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    instance = vk::raii::Instance(context, createInfo);
    violet::Log::info("Renderer", "Vulkan instance created");
}

void VulkanContext::setupDebugMessenger() {
    return;
}

void VulkanContext::createSurface(GLFWwindow* window) {
    VkSurfaceKHR rawSurface;
    if (glfwCreateWindowSurface(*instance, window, nullptr, &rawSurface) != VK_SUCCESS) {
        throw RuntimeError("Failed to create window surface");
    }
    surface = vk::raii::SurfaceKHR(instance, rawSurface);
}

void VulkanContext::pickPhysicalDevice() {
    auto devices = instance.enumeratePhysicalDevices();

    for (auto& device : devices) {
        if (isDeviceSuitable(*device)) {
            physicalDevice = std::move(device);
            break;
        }
    }

    if (physicalDevice == VK_NULL_HANDLE) {
        throw RuntimeError("Failed to find a suitable GPU");
    }

    auto properties = physicalDevice.getProperties();
    violet::Log::info("Renderer", "Selected GPU: {}", properties.deviceName.data());
}

void VulkanContext::createLogicalDevice() {
    queueFamilies = findQueueFamilies(*physicalDevice);
    
    eastl::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    eastl::set<uint32_t> uniqueQueueFamilies = {
        queueFamilies.graphicsFamily.value(),
        queueFamilies.presentFamily.value()
    };
    
    if (queueFamilies.computeFamily.has_value()) {
        uniqueQueueFamilies.insert(queueFamilies.computeFamily.value());
    }
    if (queueFamilies.transferFamily.has_value()) {
        uniqueQueueFamilies.insert(queueFamilies.transferFamily.value());
    }
    
    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        vk::DeviceQueueCreateInfo queueCreateInfo;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // Query available device features
    vk::PhysicalDeviceFeatures availableFeatures = physicalDevice.getFeatures();

    vk::PhysicalDeviceFeatures deviceFeatures;
    deviceFeatures.samplerAnisotropy = VK_TRUE;

    // Only enable features that are actually supported
    if (availableFeatures.fillModeNonSolid) {
        deviceFeatures.fillModeNonSolid = VK_TRUE;  // For wireframe rendering
        violet::Log::info("Renderer", "Enabled fillModeNonSolid feature");
    } else {
        violet::Log::warn("Renderer", "fillModeNonSolid not supported on this device");
    }

    if (availableFeatures.wideLines) {
        deviceFeatures.wideLines = VK_TRUE;         // For line width > 1.0
        violet::Log::info("Renderer", "Enabled wideLines feature");
    } else {
        violet::Log::warn("Renderer", "wideLines not supported on this device");
    }
    
    vk::PhysicalDeviceVulkan13Features features13;
    features13.dynamicRendering = VK_TRUE;

    vk::PhysicalDeviceVulkan12Features features12;
    features12.pNext = &features13;
    features12.timelineSemaphore = VK_TRUE;

    // Bindless descriptor indexing features (part of Vulkan 1.2 core)
    features12.descriptorBindingPartiallyBound = VK_TRUE;
    features12.runtimeDescriptorArray = VK_TRUE;
    features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
    features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    
    vk::DeviceCreateInfo createInfo;
    createInfo.pNext = &features12;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    createInfo.pEnabledFeatures = &deviceFeatures;

    device = vk::raii::Device(physicalDevice, createInfo);

    graphicsQueue = vk::raii::Queue(device, queueFamilies.graphicsFamily.value(), 0);
    presentQueue = vk::raii::Queue(device, queueFamilies.presentFamily.value(), 0);

    if (queueFamilies.computeFamily.has_value()) {
        computeQueue = vk::raii::Queue(device, queueFamilies.computeFamily.value(), 0);
    }
    if (queueFamilies.transferFamily.has_value()) {
        transferQueue = vk::raii::Queue(device, queueFamilies.transferFamily.value(), 0);
    }
}

bool VulkanContext::isDeviceSuitable(vk::PhysicalDevice device) {
    auto indices = findQueueFamilies(device);
    bool extensionsSupported = checkDeviceExtensionSupport(device);

    bool swapchainAdequate = false;
    if (extensionsSupported) {
        SwapchainSupportDetails details;
        details.capabilities = device.getSurfaceCapabilitiesKHR(*surface);

        auto formats = device.getSurfaceFormatsKHR(*surface);
        details.formats.clear();
        for (const auto& format : formats) {
            details.formats.push_back(format);
        }

        auto presentModes = device.getSurfacePresentModesKHR(*surface);
        details.presentModes.clear();
        for (const auto& mode : presentModes) {
            details.presentModes.push_back(mode);
        }

        swapchainAdequate = !details.formats.empty() && !details.presentModes.empty();
    }

    auto features = device.getFeatures();

    return indices.isComplete() && extensionsSupported && swapchainAdequate;
}

QueueFamilyIndices VulkanContext::findQueueFamilies(vk::PhysicalDevice device) {
    QueueFamilyIndices indices;
    auto queueFamilies = device.getQueueFamilyProperties();
    
    uint32_t i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) {
            indices.graphicsFamily = i;
        }
        
        if (queueFamily.queueFlags & vk::QueueFlagBits::eCompute && 
            !(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics)) {
            indices.computeFamily = i;
        }
        
        if (queueFamily.queueFlags & vk::QueueFlagBits::eTransfer && 
            !(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) &&
            !(queueFamily.queueFlags & vk::QueueFlagBits::eCompute)) {
            indices.transferFamily = i;
        }
        
        if (device.getSurfaceSupportKHR(i, *surface)) {
            indices.presentFamily = i;
        }
        
        i++;
    }
    
    return indices;
}

bool VulkanContext::checkDeviceExtensionSupport(vk::PhysicalDevice device) {
    auto availableExtensions = device.enumerateDeviceExtensionProperties();
    eastl::set<eastl::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());
    
    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName.data());
    }
    
    return requiredExtensions.empty();
}

SwapchainSupportDetails VulkanContext::querySwapchainSupport() const {
    SwapchainSupportDetails details;
    details.capabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);

    auto formats = physicalDevice.getSurfaceFormatsKHR(*surface);
    details.formats.clear();
    for (const auto& format : formats) {
        details.formats.push_back(format);
    }

    auto presentModes = physicalDevice.getSurfacePresentModesKHR(*surface);
    details.presentModes.clear();
    for (const auto& mode : presentModes) {
        details.presentModes.push_back(mode);
    }

    return details;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanContext::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        violet::Log::error("Renderer", "Validation: {}", pCallbackData->pMessage);
    }
    
    return VK_FALSE;
}

void VulkanContext::createCommandPool() {
    vk::CommandPoolCreateInfo poolInfo;
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolInfo.queueFamilyIndex = queueFamilies.graphicsFamily.value();

    commandPool = vk::raii::CommandPool(device, poolInfo);
}

vk::Format VulkanContext::findDepthFormat() {
    return findSupportedFormat(
        {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
        vk::ImageTiling::eOptimal,
        vk::FormatFeatureFlagBits::eDepthStencilAttachment
    );
}

vk::Format VulkanContext::findSupportedFormat(const eastl::vector<vk::Format>& candidates,
                                              vk::ImageTiling tiling, vk::FormatFeatureFlags features) {
    for (vk::Format format : candidates) {
        vk::FormatProperties props = physicalDevice.getFormatProperties(format);

        if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw RuntimeError("Failed to find supported format!");
}

void VulkanContext::createAllocator() {
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = *physicalDevice;
    allocatorInfo.device = *device;
    allocatorInfo.instance = *instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;

    if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS) {
        throw RuntimeError("Failed to create VMA allocator");
    }

    violet::Log::info("Renderer", "VMA allocator created");
}

}
