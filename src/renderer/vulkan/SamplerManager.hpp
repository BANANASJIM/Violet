#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/unordered_map.h>

namespace violet {

class VulkanContext;

enum class SamplerType {
    Default,        // Linear, Repeat, Anisotropy enabled
    ClampToEdge,    // Linear, ClampToEdge, No anisotropy (for PostProcess)
    Nearest,        // Nearest, Repeat, No anisotropy
    Shadow,         // Linear, ClampToBorder, CompareOp enabled
    Cubemap,        // Linear, ClampToEdge, No anisotropy (for skybox/environment)
    NearestClamp    // Nearest, ClampToEdge, No anisotropy
};

struct SamplerConfig {
    vk::Filter magFilter = vk::Filter::eLinear;
    vk::Filter minFilter = vk::Filter::eLinear;
    vk::SamplerAddressMode addressModeU = vk::SamplerAddressMode::eRepeat;
    vk::SamplerAddressMode addressModeV = vk::SamplerAddressMode::eRepeat;
    vk::SamplerAddressMode addressModeW = vk::SamplerAddressMode::eRepeat;
    vk::SamplerMipmapMode mipmapMode = vk::SamplerMipmapMode::eLinear;
    float minLod = 0.0f;
    float maxLod = VK_LOD_CLAMP_NONE;
    float mipLodBias = 0.0f;
    bool anisotropyEnable = false;
    float maxAnisotropy = 1.0f;
    vk::BorderColor borderColor = vk::BorderColor::eFloatOpaqueBlack;
    bool compareEnable = false;
    vk::CompareOp compareOp = vk::CompareOp::eNever;

    size_t hash() const;
    bool operator==(const SamplerConfig& other) const;

    static SamplerConfig getDefault(float maxAnisotropy);
    static SamplerConfig getClampToEdge();
    static SamplerConfig getNearest();
    static SamplerConfig getShadow();
    static SamplerConfig getCubemap();
    static SamplerConfig getNearestClamp();
};

class SamplerManager {
public:
    void init(VulkanContext* context);
    void cleanup();

    // Get or create a sampler based on predefined type
    vk::Sampler getSampler(SamplerType type);

    // Get or create a sampler with custom configuration
    vk::Sampler getOrCreateSampler(const SamplerConfig& config);

private:
    vk::Sampler createSampler(const SamplerConfig& config);

    VulkanContext* context = nullptr;
    eastl::unordered_map<size_t, vk::Sampler> samplerCache;          // config hash -> sampler
    eastl::unordered_map<SamplerType, vk::Sampler> predefinedSamplers; // type -> sampler
};

} // namespace violet