#include "renderer/vulkan/SamplerManager.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "core/Log.hpp"
#include <functional>

namespace violet {

size_t SamplerConfig::hash() const {
    size_t h = 0;
    h ^= std::hash<int>{}(static_cast<int>(magFilter)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(static_cast<int>(minFilter)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(static_cast<int>(addressModeU)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(static_cast<int>(addressModeV)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(static_cast<int>(addressModeW)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(static_cast<int>(mipmapMode)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<float>{}(minLod) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<float>{}(maxLod) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<bool>{}(anisotropyEnable) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<float>{}(maxAnisotropy) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(static_cast<int>(borderColor)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<bool>{}(compareEnable) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(static_cast<int>(compareOp)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

bool SamplerConfig::operator==(const SamplerConfig& other) const {
    return magFilter == other.magFilter &&
           minFilter == other.minFilter &&
           addressModeU == other.addressModeU &&
           addressModeV == other.addressModeV &&
           addressModeW == other.addressModeW &&
           mipmapMode == other.mipmapMode &&
           minLod == other.minLod &&
           maxLod == other.maxLod &&
           mipLodBias == other.mipLodBias &&
           anisotropyEnable == other.anisotropyEnable &&
           maxAnisotropy == other.maxAnisotropy &&
           borderColor == other.borderColor &&
           compareEnable == other.compareEnable &&
           compareOp == other.compareOp;
}

SamplerConfig SamplerConfig::getDefault(float maxAnisotropy) {
    SamplerConfig config;
    config.magFilter = vk::Filter::eLinear;
    config.minFilter = vk::Filter::eLinear;
    config.addressModeU = vk::SamplerAddressMode::eRepeat;
    config.addressModeV = vk::SamplerAddressMode::eRepeat;
    config.addressModeW = vk::SamplerAddressMode::eRepeat;
    config.mipmapMode = vk::SamplerMipmapMode::eLinear;
    config.anisotropyEnable = true;
    config.maxAnisotropy = maxAnisotropy;
    return config;
}

SamplerConfig SamplerConfig::getClampToEdge() {
    SamplerConfig config;
    config.magFilter = vk::Filter::eLinear;
    config.minFilter = vk::Filter::eLinear;
    config.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    config.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    config.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    config.mipmapMode = vk::SamplerMipmapMode::eLinear;
    config.anisotropyEnable = false;
    config.maxAnisotropy = 1.0f;
    return config;
}

SamplerConfig SamplerConfig::getNearest() {
    SamplerConfig config;
    config.magFilter = vk::Filter::eNearest;
    config.minFilter = vk::Filter::eNearest;
    config.addressModeU = vk::SamplerAddressMode::eRepeat;
    config.addressModeV = vk::SamplerAddressMode::eRepeat;
    config.addressModeW = vk::SamplerAddressMode::eRepeat;
    config.mipmapMode = vk::SamplerMipmapMode::eNearest;
    config.anisotropyEnable = false;
    config.maxAnisotropy = 1.0f;
    return config;
}

SamplerConfig SamplerConfig::getShadow() {
    SamplerConfig config;
    config.magFilter = vk::Filter::eLinear;
    config.minFilter = vk::Filter::eLinear;
    config.addressModeU = vk::SamplerAddressMode::eClampToBorder;
    config.addressModeV = vk::SamplerAddressMode::eClampToBorder;
    config.addressModeW = vk::SamplerAddressMode::eClampToBorder;
    config.borderColor = vk::BorderColor::eFloatOpaqueWhite;
    config.compareEnable = true;
    config.compareOp = vk::CompareOp::eLessOrEqual;
    config.anisotropyEnable = false;
    config.maxAnisotropy = 1.0f;
    return config;
}

SamplerConfig SamplerConfig::getCubemap() {
    SamplerConfig config;
    config.magFilter = vk::Filter::eLinear;
    config.minFilter = vk::Filter::eLinear;
    config.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    config.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    config.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    config.mipmapMode = vk::SamplerMipmapMode::eLinear;
    config.anisotropyEnable = false;
    config.maxAnisotropy = 1.0f;
    return config;
}

SamplerConfig SamplerConfig::getNearestClamp() {
    SamplerConfig config;
    config.magFilter = vk::Filter::eNearest;
    config.minFilter = vk::Filter::eNearest;
    config.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    config.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    config.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    config.mipmapMode = vk::SamplerMipmapMode::eNearest;
    config.anisotropyEnable = false;
    config.maxAnisotropy = 1.0f;
    return config;
}

void SamplerManager::init(VulkanContext* ctx) {
    context = ctx;
    Log::info("Renderer", "SamplerManager initialized");
}

void SamplerManager::cleanup() {
    auto device = context->getDevice();

    for (auto& [hash, sampler] : samplerCache) {
        device.destroySampler(sampler);
    }
    samplerCache.clear();
    predefinedSamplers.clear();

    Log::info("Renderer", "SamplerManager cleaned up");
}

vk::Sampler SamplerManager::createSampler(const SamplerConfig& config) {
    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = config.magFilter;
    samplerInfo.minFilter = config.minFilter;
    samplerInfo.addressModeU = config.addressModeU;
    samplerInfo.addressModeV = config.addressModeV;
    samplerInfo.addressModeW = config.addressModeW;
    samplerInfo.mipmapMode = config.mipmapMode;
    samplerInfo.minLod = config.minLod;
    samplerInfo.maxLod = config.maxLod;
    samplerInfo.mipLodBias = config.mipLodBias;
    samplerInfo.anisotropyEnable = config.anisotropyEnable;
    samplerInfo.maxAnisotropy = config.maxAnisotropy;
    samplerInfo.borderColor = config.borderColor;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = config.compareEnable;
    samplerInfo.compareOp = config.compareOp;

    return context->getDevice().createSampler(samplerInfo);
}

vk::Sampler SamplerManager::getOrCreateSampler(const SamplerConfig& config) {
    size_t configHash = config.hash();

    // Check cache first
    auto it = samplerCache.find(configHash);
    if (it != samplerCache.end()) {
        return it->second;
    }

    // Create new sampler and cache it
    vk::Sampler sampler = createSampler(config);
    samplerCache[configHash] = sampler;

    Log::debug("Renderer", "Created and cached new sampler (hash: {}, total: {})",
              configHash, samplerCache.size());

    return sampler;
}

vk::Sampler SamplerManager::getSampler(SamplerType type) {
    // Check if predefined sampler already exists
    auto it = predefinedSamplers.find(type);
    if (it != predefinedSamplers.end()) {
        return it->second;
    }

    // Create sampler based on type
    SamplerConfig config;
    vk::PhysicalDeviceProperties properties = context->getPhysicalDevice().getProperties();

    switch (type) {
        case SamplerType::Default:
            config = SamplerConfig::getDefault(properties.limits.maxSamplerAnisotropy);
            break;
        case SamplerType::ClampToEdge:
            config = SamplerConfig::getClampToEdge();
            break;
        case SamplerType::Nearest:
            config = SamplerConfig::getNearest();
            break;
        case SamplerType::Shadow:
            config = SamplerConfig::getShadow();
            break;
        case SamplerType::Cubemap:
            config = SamplerConfig::getCubemap();
            break;
        case SamplerType::NearestClamp:
            config = SamplerConfig::getNearestClamp();
            break;
        default:
            Log::warn("Renderer", "Unknown sampler type, using default");
            config = SamplerConfig::getDefault(properties.limits.maxSamplerAnisotropy);
            break;
    }

    vk::Sampler sampler = getOrCreateSampler(config);
    predefinedSamplers[type] = sampler;

    Log::info("Renderer", "Created predefined sampler type {}", static_cast<int>(type));

    return sampler;
}

} // namespace violet