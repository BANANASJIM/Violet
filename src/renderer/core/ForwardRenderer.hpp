#pragma once

#include "renderer/BaseRenderer.hpp"
#include <vulkan/vulkan.hpp>

#include <glm/glm.hpp>

#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>

#include <entt/entt.hpp>

#include "renderer/DescriptorSet.hpp"
#include "renderer/descriptor/DescriptorManager.hpp"
#include "resource/Material.hpp"
#include "resource/MaterialManager.hpp"
#include "renderer/Renderable.hpp"
#include "resource/Texture.hpp"
#include "resource/gpu/UniformBuffer.hpp"
#include "renderer/core/DebugRenderer.hpp"
#include "renderer/effect/EnvironmentMap.hpp"
#include "renderer/core/RenderPass.hpp"
#include "acceleration/BVH.hpp"

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
    void           update(entt::registry& world, uint32_t frameIndex, float skyboxExposure = 1.0f, float skyboxRotation = 0.0f, bool skyboxEnabled = false);
    DescriptorSet* getDescriptorSet() const { return descriptorSet.get(); }
    Camera* findActiveCamera(entt::registry& world);  // Made public for frustum culling
    void setSkyboxTexture(Texture* texture);

private:

    VulkanContext*                                  context = nullptr;
    eastl::unique_ptr<DescriptorSet>                descriptorSet;
    eastl::vector<eastl::unique_ptr<UniformBuffer>> uniformBuffers;
    static constexpr uint32_t MAX_LIGHTS = 8;
    struct GlobalUBO {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 proj;
        alignas(16) glm::vec3 cameraPos;
        alignas(4) float padding0;

        // Light data (support up to MAX_LIGHTS lights)
        alignas(16) glm::vec4 lightPositions[MAX_LIGHTS];  // xyz=position/direction, w=type (0=dir, 1=point)
        alignas(16) glm::vec4 lightColors[MAX_LIGHTS];     // xyz=color*intensity, w=radius (for point lights)
        alignas(16) glm::vec4 lightParams[MAX_LIGHTS];     // x=linear, y=quadratic attenuation, zw=reserved
        alignas(4) int numLights;
        alignas(16) glm::vec3 ambientLight;  // Ambient light color

        // Skybox data
        alignas(4) float skyboxExposure;
        alignas(4) float skyboxRotation;
        alignas(4) int skyboxEnabled;
        alignas(4) float padding1;
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
    // Get final pass RenderPass for swapchain framebuffer creation
    vk::RenderPass getFinalPassRenderPass() const;

    // Pass system interface
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

    void onSwapchainRecreate(vk::Extent2D newExtent) override;

    // Multi-pass system
    void setupPasses(vk::Format swapchainFormat);
    const eastl::vector<eastl::unique_ptr<Pass>>& getPasses() const { return passes; }
    Pass* getPass(size_t index) { return passes[index].get(); }
    RenderPass* getRenderPass(size_t index);  // Helper to get RenderPass specifically

    // Skybox access
    // TODO: Re-enable after EnvironmentMap redesign
    // EnvironmentMap& getEnvironmentMap() { return environmentMap; }
    GlobalUniforms& getGlobalUniforms() { return globalUniforms; }

    // PostProcess access
    Material* getPostProcessMaterial() const { return postProcessMaterial; }
    void updatePostProcessDescriptors();  // Update descriptor set with offscreen textures

    // PBR Bindless Material access (shared material for all PBR instances)
    Material* getPBRBindlessMaterial() const { return pbrBindlessMaterial; }

    // Descriptor manager access
    DescriptorManager& getDescriptorManager() { return descriptorManager; }

    // Compatibility layer - delegates to MaterialManager
    MaterialInstance* getMaterialInstanceByIndex(uint32_t index) const;

private:
    void collectFromEntity(entt::entity entity, entt::registry& world);

    // Declarative descriptor layouts registration
    void registerDescriptorLayouts();

    // Declarative pass helpers
    void insertPassTransition(vk::CommandBuffer cmd, size_t passIndex);

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

    // Material references from MaterialManager (not owned by renderer)
    Material* postProcessMaterial = nullptr;
    Material* pbrBindlessMaterial = nullptr;

    // Descriptor sets owned by renderer
    eastl::unique_ptr<DescriptorSet> postProcessDescriptorSet;

    GlobalUniforms globalUniforms;
    DebugRenderer debugRenderer;
    // TODO: Redesign EnvironmentMap with bindless architecture
    // EnvironmentMap environmentMap;
    eastl::vector<eastl::unique_ptr<Pass>> passes;

    DescriptorManager descriptorManager;
    ResourceManager* resourceManager = nullptr;  // Injected dependency

};

} // namespace violet