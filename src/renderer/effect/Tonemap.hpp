#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace violet {

class VulkanContext;
class DescriptorManager;
class MaterialManager;
class RenderGraph;
class Material;

// Tonemap operator modes
enum class TonemapMode : uint32_t {
    ACESFitted = 0,      // UE4/UE5 default, most accurate (with color space transforms)
    ACESNarkowicz = 1,   // Fast approximation
    Uncharted2 = 2,      // Classic game industry standard
    Reinhard = 3,        // Simple, fast, can wash out
    None = 4             // Linear (for debugging)
};

// Tonemap parameters
struct TonemapParams {
    float ev100 = 9.0f;           // Exposure Value at ISO 100 (sunny day default)
    float gamma = 2.2f;           // Gamma correction (default 2.2 for sRGB)
    TonemapMode mode = TonemapMode::ACESFitted;  // Tonemap operator mode

    // EV100 limits for clamping
    float minEV100 = -2.0f;       // Night scene
    float maxEV100 = 16.0f;       // Direct sunlight
};

// Tonemap post-processing effect
// Applies tone mapping and gamma correction to HDR image
// Integrates with AutoExposure for automatic EV100 values
// Uses MaterialManager-owned pipeline, RenderGraph for resource management
class Tonemap {
public:
    void init(VulkanContext* context, MaterialManager* materialManager,
              DescriptorManager* descriptorManager, RenderGraph* renderGraph,
              const eastl::string& hdrImageName, const eastl::string& swapchainImageName);
    void cleanup();

    // RenderGraph integration
    void addToRenderGraph();

    // Parameter access
    TonemapParams& getParams() { return params; }
    const TonemapParams& getParams() const { return params; }
    void setEV100(float ev100) { params.ev100 = glm::clamp(ev100, params.minEV100, params.maxEV100); }
    void setGamma(float gamma) { params.gamma = gamma; }
    void setMode(TonemapMode mode) { params.mode = mode; }

    float getEV100() const { return params.ev100; }
    float getGamma() const { return params.gamma; }
    TonemapMode getMode() const { return params.mode; }

private:
    void executePass(vk::CommandBuffer cmd, uint32_t frameIndex);

    VulkanContext* context = nullptr;
    MaterialManager* materialManager = nullptr;
    DescriptorManager* descriptorManager = nullptr;
    RenderGraph* renderGraph = nullptr;

    Material* postProcessMaterial = nullptr;  // Reference from MaterialManager, not owned
    eastl::vector<vk::DescriptorSet> descriptorSets;  // One per frame in flight (triple buffering)

    TonemapParams params;

    // Resource names for RenderGraph
    eastl::string hdrImageName;
    eastl::string swapchainImageName;

    // Cache descriptor updates per frame (triple buffering requires separate cache per frame)
    eastl::vector<vk::ImageView> cachedHDRViews;
    eastl::vector<vk::ImageView> cachedDepthViews;
};

} // namespace violet
