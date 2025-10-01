#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>

namespace violet {

// Simplified render settings - only essential quality options
struct RenderSettings {
    // Anisotropic filtering
    bool enableAnisotropy = true;
    float maxAnisotropy = 16.0f;  // Will be clamped to device max

    // MSAA (note: requires render target recreation - not yet implemented)
    vk::SampleCountFlagBits msaaSamples = vk::SampleCountFlagBits::e1;

    // Get default settings based on device capabilities
    static RenderSettings getDefaults(vk::PhysicalDevice physicalDevice) {
        RenderSettings settings;
        vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
        settings.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
        return settings;
    }

    // Load settings from config file
    static RenderSettings loadFromFile(const eastl::string& configPath, vk::PhysicalDevice physicalDevice);
};

}