#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/array.h>
#include <EASTL/string.h>
#include "resource/TextureManager.hpp"

namespace violet {

class VulkanContext;
class MaterialManager;
class DescriptorManager;
class TextureManager;
class Texture;
class DescriptorSet;
class ShaderLibrary;

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

    // Initialization (all resources managed by injected managers)
    void init(VulkanContext* context, MaterialManager* matMgr, DescriptorManager* descMgr, TextureManager* texMgr, ShaderLibrary* shaderLib);
    void cleanup();

    // Loading methods
    void loadHDR(const eastl::string& hdrPath);
    void loadCubemap(const eastl::array<eastl::string, 6>& facePaths);

    // IBL generation
    void generateIBLMaps();

    // Bindless texture indices for GlobalUBO
    uint32_t getEnvironmentMapIndex() const { return environmentMapIndex; }
    uint32_t getIrradianceMapIndex() const { return irradianceMapIndex; }
    uint32_t getPrefilteredMapIndex() const { return prefilteredMapIndex; }
    uint32_t getBRDFLUTIndex() const { return brdfLUTIndex; }

    // Parameter management
    void setExposure(float exposure) { params.exposure = exposure; }
    void setRotation(float rotation) { params.rotation = rotation; }
    void setIntensity(float intensity) { params.intensity = intensity; }
    void setEnabled(bool enabled) { params.enabled = enabled && environmentTextureHandle.isValid(); }

    float getExposure() const { return params.exposure; }
    float getRotation() const { return params.rotation; }
    float getIntensity() const { return params.intensity; }
    bool isEnabled() const { return params.enabled; }

    // Texture access (for inspection/debugging) - returns raw pointers from TextureManager
    Texture* getEnvironmentTexture() const;
    Texture* getIrradianceMap() const;
    Texture* getPrefilteredMap() const;
    Texture* getBRDFLUT() const;

private:
    // Core resources (injected dependencies)
    VulkanContext* context = nullptr;
    MaterialManager* materialManager = nullptr;
    DescriptorManager* descriptorManager = nullptr;
    TextureManager* textureManager = nullptr;
    ShaderLibrary* shaderLibrary = nullptr;

    // Texture handles (references to TextureManager-owned resources)
    TextureHandle environmentTextureHandle;
    TextureHandle irradianceMapHandle;
    TextureHandle prefilteredMapHandle;
    TextureHandle brdfLUTHandle;

    // Bindless indices (0 = invalid/not loaded)
    uint32_t environmentMapIndex = 0;
    uint32_t irradianceMapIndex = 0;
    uint32_t prefilteredMapIndex = 0;
    uint32_t brdfLUTIndex = 0;

    // Helper methods (use DescriptorManager for compute pipelines)
    void generateCubemapFromEquirect(const eastl::string& hdrPath, uint32_t cubemapSize);
    void generateIrradianceMap();
    void generatePrefilteredMap();
    void generateBRDFLUT();

    // Temporary compute resources (kept alive to prevent validation errors)
    eastl::vector<eastl::unique_ptr<Texture>> tempComputeTextures;
    eastl::vector<eastl::unique_ptr<DescriptorSet>> tempDescriptorSets;
    eastl::vector<vk::raii::ImageView> tempImageViews;

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