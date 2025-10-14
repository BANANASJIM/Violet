#pragma once

#include "renderer/BaseRenderer.hpp"
#include <vulkan/vulkan.hpp>

#include <glm/glm.hpp>

#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>

#include <entt/entt.hpp>

#include "renderer/vulkan/DescriptorSet.hpp"
#include "renderer/vulkan/DescriptorManager.hpp"
#include "resource/Material.hpp"
#include "resource/MaterialManager.hpp"
#include "renderer/Renderable.hpp"
#include "resource/Texture.hpp"
#include "resource/gpu/UniformBuffer.hpp"
#include "renderer/DebugRenderer.hpp"
#include "renderer/effect/EnvironmentMap.hpp"
#include "renderer/effect/AutoExposure.hpp"
#include "renderer/graph/RenderPass.hpp"
#include "acceleration/BVH.hpp"
#include "renderer/graph/RenderGraph.hpp"

namespace violet {

// Descriptor Set and Binding 约定常量
constexpr uint32_t GLOBAL_SET   = 0; // set = 0: 全局数据(相机、光照)
constexpr uint32_t MATERIAL_SET = 1; // set = 1: 材质数据(纹理、材质参数)

constexpr uint32_t CAMERA_UBO_BINDING         = 0; // Global set binding 0: 相机变换矩阵
constexpr uint32_t BASE_COLOR_TEXTURE_BINDING = 0; // Material set binding 0: 基础颜色纹理

class Camera;
class ResourceManager;

// Rendering statistics structure
struct RenderStats {
    uint32_t totalRenderables = 0;
    uint32_t visibleRenderables = 0;
    uint32_t drawCalls = 0;
    uint32_t skippedRenderables = 0;
};

class GlobalUniforms {
public:
    ~GlobalUniforms();
    void           init(VulkanContext* context, DescriptorManager* descMgr, uint32_t maxFramesInFlight);
    void           cleanup();
    void           update(entt::registry& world, uint32_t frameIndex, float skyboxExposure = 1.0f, float skyboxRotation = 0.0f, bool skyboxEnabled = false, float iblIntensity = 1.0f);
    DescriptorSet* getDescriptorSet() const { return descriptorSet.get(); }
    Camera* findActiveCamera(entt::registry& world);  // Made public for frustum culling
    void setSkyboxTexture(Texture* texture);
    void setIBLIndices(uint32_t envMap, uint32_t irradiance, uint32_t prefiltered, uint32_t brdfLUT);

private:

    VulkanContext*                                  context = nullptr;
    eastl::unique_ptr<DescriptorSet>                descriptorSet;
    eastl::vector<eastl::unique_ptr<UniformBuffer>> uniformBuffers;

    // IBL bindless indices (cached values)
    uint32_t iblEnvironmentMapIndex = 0;
    uint32_t iblIrradianceMapIndex = 0;
    uint32_t iblPrefilteredMapIndex = 0;
    uint32_t iblBRDFLUTIndex = 0;

    static constexpr uint32_t MAX_LIGHTS = 8;
    struct GlobalUBO {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 proj;
        alignas(16) glm::vec3 cameraPos;
        alignas(4) float padding0;

        // Light data (support up to MAX_LIGHTS lights)
        // Physical units: DirectionalLight uses lux, PointLight uses lumens
        alignas(16) glm::vec4 lightPositions[MAX_LIGHTS];  // xyz=position/direction, w=type (0=dir, 1=point)
        alignas(16) glm::vec4 lightColors[MAX_LIGHTS];     // xyz=color*intensity (physical units), w=radius
        alignas(4) int numLights;
        alignas(16) glm::vec3 ambientLight;  // Ambient light color

        // Skybox data
        alignas(4) float skyboxExposure;
        alignas(4) float skyboxRotation;
        alignas(4) int skyboxEnabled;
        alignas(4) float iblIntensity;

        // IBL bindless texture indices
        alignas(4) uint32_t environmentMapIndex;
        alignas(4) uint32_t irradianceMapIndex;
        alignas(4) uint32_t prefilteredMapIndex;
        alignas(4) uint32_t brdfLUTIndex;
    } cachedUBO;
};

class ForwardRenderer : public BaseRenderer {
public:
    ForwardRenderer() = default;
    ~ForwardRenderer() override;

    ForwardRenderer(const ForwardRenderer&)            = delete;
    ForwardRenderer& operator=(const ForwardRenderer&) = delete;

    ForwardRenderer(ForwardRenderer&&)            = delete;
    ForwardRenderer& operator=(ForwardRenderer&&) = delete;

    void init(VulkanContext* context, ResourceManager* resMgr, vk::Format wapchainFormat, uint32_t maxFramesInFlight);
    void cleanup();

    void createMaterials();  // Create materials after MaterialManager is initialized

    // Frame rendering
    void beginFrame(entt::registry& world, uint32_t frameIndex);
    void renderFrame(vk::CommandBuffer cmd, vk::Framebuffer framebuffer, vk::Extent2D extent, uint32_t frameIndex);
    void endFrame();

    void collectRenderables(entt::registry& world);
    void updateGlobalUniforms(entt::registry& world, uint32_t frameIndex);
    void renderScene(vk::CommandBuffer commandBuffer, uint32_t frameIndex, entt::registry& world);

    DescriptorSet* getGlobalDescriptorSet() const { return globalUniforms.getDescriptorSet(); }

    // Resource manager access
    ResourceManager* getResourceManager() { return resourceManager; }
    const ResourceManager* getResourceManager() const { return resourceManager; }

    // Material manager access (convenience wrapper)
    MaterialManager* getMaterialManager();
    const MaterialManager* getMaterialManager() const;

    void                             clearRenderables() { renderables.clear(); }
    const eastl::vector<Renderable>& getRenderables() const { return renderables; }

    // BVH management
    void buildSceneBVH(entt::registry& world);

    // Debug rendering
    DebugRenderer& getDebugRenderer() { return debugRenderer; }

    // Statistics access
    const RenderStats& getRenderStats() const { return renderStats; }

    // Scene state management
    void markSceneDirty() { sceneDirty = true; }

    // Swapchain resize
    void resize(vk::Extent2D newExtent);

    // Skybox access
    EnvironmentMap& getEnvironmentMap() { return environmentMap; }
    GlobalUniforms& getGlobalUniforms() { return globalUniforms; }

    // Auto-exposure access
    AutoExposure& getAutoExposure() { return autoExposure; }

    // todo remove
    // PostProcess tone mapping parameters (EV100 system)
    void setPostProcessEV100(float ev) { postProcessEV100 = ev; }
    void setPostProcessGamma(float gamma) { postProcessGamma = gamma; }
    void setTonemapMode(uint32_t mode) { tonemapMode = mode; }
    float getPostProcessEV100() const { return postProcessEV100; }
    float getPostProcessGamma() const { return postProcessGamma; }
    uint32_t getTonemapMode() const { return tonemapMode; }

    // PBR Bindless Material access (shared material for all PBR instances)
    Material* getPBRBindlessMaterial() const { return pbrBindlessMaterial; }

    // Descriptor manager access (delegates to ResourceManager)
    DescriptorManager& getDescriptorManager();

    // Compatibility layer - delegates to MaterialManager
    MaterialInstance* getMaterialInstanceByIndex(uint32_t index) const;

private:
    void collectFromEntity(entt::entity entity, entt::registry& world);

    // Declarative descriptor layouts registration
    void registerDescriptorLayouts();

    // Cleanup protection
    bool isCleanedUp = false;

    eastl::vector<Renderable>                          renderables;
    eastl::vector<AABB>                                renderableBounds;
    eastl::hash_map<entt::entity, eastl::vector<uint32_t>> renderableCache;
    BVH sceneBVH;
    eastl::vector<uint32_t> visibleIndices;
    bool sceneDirty = true;
    bool bvhBuilt = false;
    RenderStats renderStats;
    entt::registry* currentWorld = nullptr;
    vk::Extent2D currentExtent = {1280, 720};
    uint32_t currentFrameIndex = 0;
    // Descriptor sets owned by renderer
    eastl::unique_ptr<DescriptorSet> postProcessDescriptorSet;

    // todo move this to specific class
    // PostProcess tone mapping parameters (EV100 system)
    // EV100 = Exposure Value at ISO 100 (photographic exposure)
    // Typical values: -2 (night), 0 (overcast), 9-10 (sunny), 15 (direct sun)
    float postProcessEV100 = 9.0f;  // Default: sunny day
    float postProcessGamma = 2.2f;
    uint32_t tonemapMode = 0;  // 0=ACES Fitted (default), 1=ACES Narkowicz, 2=Uncharted2, 3=Reinhard, 4=None

    GlobalUniforms globalUniforms;
    DebugRenderer debugRenderer;
    EnvironmentMap environmentMap;
    AutoExposure autoExposure;

    // Render graph for automatic resource and barrier management
    eastl::unique_ptr<RenderGraph> renderGraph;

    ResourceManager* resourceManager = nullptr;

    // Time tracking for auto-exposure
    std::chrono::steady_clock::time_point lastFrameTime;

};

} // namespace violet