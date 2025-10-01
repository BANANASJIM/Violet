#include "RenderSettings.hpp"
#include "core/Log.hpp"
#include "core/FileSystem.hpp"

#define JSON_HAS_CPP_17
#include <nlohmann/json.hpp>

#include <fstream>

namespace violet {

RenderSettings RenderSettings::loadFromFile(const eastl::string& configPath, vk::PhysicalDevice physicalDevice) {
    // Start with device defaults
    RenderSettings settings = getDefaults(physicalDevice);

    try {
        // Resolve config path
        eastl::string resolvedPath = violet::FileSystem::resolveRelativePath(configPath);

        // Open and parse JSON file
        std::ifstream configFile(resolvedPath.c_str());
        if (!configFile.is_open()) {
            violet::Log::warn("Renderer", "Config file not found: {}, using defaults", resolvedPath.c_str());
            return settings;
        }

        nlohmann::json config = nlohmann::json::parse(configFile);

        // Load renderer settings
        if (config.contains("renderer")) {
            auto& rendererConfig = config["renderer"];

            // Load anisotropic filtering settings
            if (rendererConfig.contains("anisotropicFiltering")) {
                auto& anisoConfig = rendererConfig["anisotropicFiltering"];

                if (anisoConfig.contains("enabled")) {
                    settings.enableAnisotropy = anisoConfig["enabled"].get<bool>();
                }

                if (anisoConfig.contains("maxAnisotropy")) {
                    float configValue = anisoConfig["maxAnisotropy"].get<float>();
                    vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
                    // Clamp to device max
                    settings.maxAnisotropy = std::min(configValue, properties.limits.maxSamplerAnisotropy);

                    if (configValue > properties.limits.maxSamplerAnisotropy) {
                        violet::Log::warn("Renderer", "Config maxAnisotropy {:.1f}x exceeds device max {:.1f}x, clamping",
                                          configValue, properties.limits.maxSamplerAnisotropy);
                    }
                }
            }

            // Load MSAA settings
            if (rendererConfig.contains("msaa")) {
                auto& msaaConfig = rendererConfig["msaa"];

                bool msaaEnabled = false;
                if (msaaConfig.contains("enabled")) {
                    msaaEnabled = msaaConfig["enabled"].get<bool>();
                }

                if (msaaEnabled && msaaConfig.contains("samples")) {
                    int requestedSamples = msaaConfig["samples"].get<int>();

                    // Map to VkSampleCountFlagBits
                    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
                    switch (requestedSamples) {
                        case 1:  samples = vk::SampleCountFlagBits::e1; break;
                        case 2:  samples = vk::SampleCountFlagBits::e2; break;
                        case 4:  samples = vk::SampleCountFlagBits::e4; break;
                        case 8:  samples = vk::SampleCountFlagBits::e8; break;
                        default:
                            violet::Log::warn("Renderer", "Invalid MSAA samples {}, defaulting to 1x", requestedSamples);
                            samples = vk::SampleCountFlagBits::e1;
                    }

                    // Check device support
                    vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
                    vk::SampleCountFlags supportedSamples = properties.limits.framebufferColorSampleCounts &
                                                            properties.limits.framebufferDepthSampleCounts;

                    if (supportedSamples & samples) {
                        settings.msaaSamples = samples;
                    } else {
                        violet::Log::warn("Renderer", "MSAA {}x not supported by device, defaulting to 1x", requestedSamples);
                        settings.msaaSamples = vk::SampleCountFlagBits::e1;
                    }
                } else {
                    settings.msaaSamples = vk::SampleCountFlagBits::e1;
                }
            }
        }

        // Format MSAA samples for logging
        int msaaSamplesInt = static_cast<int>(settings.msaaSamples);

        violet::Log::info("Renderer", "Loaded config from {}: anisotropy={}, maxAnisotropy={:.0f}x, MSAA={}x",
                          configPath.c_str(),
                          settings.enableAnisotropy ? "enabled" : "disabled",
                          settings.maxAnisotropy,
                          msaaSamplesInt);

    } catch (const nlohmann::json::exception& e) {
        violet::Log::error("Renderer", "Failed to parse config file {}: {}", configPath.c_str(), e.what());
    } catch (const std::exception& e) {
        violet::Log::error("Renderer", "Failed to load config file {}: {}", configPath.c_str(), e.what());
    }

    return settings;
}

} // namespace violet
