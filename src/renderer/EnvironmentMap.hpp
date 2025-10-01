#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/unique_ptr.h>
#include <EASTL/array.h>
#include <EASTL/string.h>

namespace violet {

class VulkanContext;
class RenderPass;
class Material;
class Texture;
class ForwardRenderer;
class ComputePipeline;
class DescriptorSet;

class EnvironmentMap {
public:
    enum class Type {
        Equirectangular,  // HDR panoramic format
        Cubemap          // Cubemap format
    };

    EnvironmentMap();
    ~EnvironmentMap();

    // Delete copy operations
    EnvironmentMap(const EnvironmentMap&) = delete;
    EnvironmentMap& operator=(const EnvironmentMap&) = delete;

    // Enable move operations
    EnvironmentMap(EnvironmentMap&& other) noexcept;
    EnvironmentMap& operator=(EnvironmentMap&& other) noexcept;

    // Initialization
    void init(VulkanContext* context, RenderPass* renderPass, ForwardRenderer* renderer);
    void cleanup();

    // Loading methods
    void loadHDR(const eastl::string& hdrPath);
    void loadCubemap(const eastl::array<eastl::string, 6>& facePaths);
    void setTexture(eastl::unique_ptr<Texture> texture);

    // IBL generation (future)
    void generateIBLMaps();

    // Rendering
    void renderSkybox(vk::CommandBuffer commandBuffer, uint32_t frameIndex,
                     vk::PipelineLayout pipelineLayout, vk::DescriptorSet globalDescriptorSet);

    // Parameter management (compatible with old Skybox interface)
    void setExposure(float exposure) { params.exposure = exposure; }
    void setRotation(float rotation) { params.rotation = rotation; }
    void setIntensity(float intensity) { params.intensity = intensity; }
    void setEnabled(bool enabled) { params.enabled = enabled && (environmentTexture != nullptr); }

    float getExposure() const { return params.exposure; }
    float getRotation() const { return params.rotation; }
    float getIntensity() const { return params.intensity; }
    bool isEnabled() const { return params.enabled; }

    // Texture access
    Texture* getEnvironmentTexture() const { return environmentTexture.get(); }
    Texture* getIrradianceMap() const { return irradianceMap.get(); }
    Texture* getPrefilteredMap() const { return prefilteredMap.get(); }
    Texture* getBRDFLUT() const { return brdfLUT.get(); }

    // Material access
    Material* getMaterial() const { return skyboxMaterial; }

private:
    // Core resources
    VulkanContext* context = nullptr;
    RenderPass* renderPass = nullptr;
    ForwardRenderer* renderer = nullptr;

    // Textures
    eastl::unique_ptr<Texture> environmentTexture;  // Main environment map (cubemap or 2D)
    eastl::unique_ptr<Texture> equirectTexture;     // Temporary equirectangular texture (for compute shader input)
    eastl::unique_ptr<Texture> irradianceMap;      // Diffuse irradiance for IBL (future)
    eastl::unique_ptr<Texture> prefilteredMap;     // Specular prefiltered for IBL (future)
    eastl::unique_ptr<Texture> brdfLUT;            // BRDF lookup table (future)

    // Rendering
    Material* skyboxMaterial = nullptr;

    // Compute pipeline for equirectangular to cubemap conversion
    eastl::unique_ptr<ComputePipeline> equirectToCubemapPipeline;
    eastl::unique_ptr<DescriptorSet> computeDescriptorSet;

    // Helper method for GPU-based cubemap generation
    void generateCubemapFromEquirect(Texture* equirectTexture, Texture* cubemapTexture, uint32_t cubemapSize);

    // Parameters
    struct Parameters {
        float exposure = 1.0f;
        float rotation = 0.0f;
        float intensity = 1.0f;
        bool enabled = false;
    } params;

    Type currentType = Type::Cubemap;
};

} // namespace violet