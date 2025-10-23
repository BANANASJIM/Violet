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
#include "renderer/effect/Tonemap.hpp"
#include "renderer/ShadowPass.hpp"
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

// GlobalUniforms class removed - now using DescriptorManager::createUniform() + UniformHandle

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

    // Frame rendering
    void beginFrame(entt::registry& world, uint32_t frameIndex);
    void renderFrame(vk::CommandBuffer cmd, uint32_t imageIndex, vk::Extent2D extent, uint32_t frameIndex);
    void endFrame();

    // RenderGraph setup
    void rebuildRenderGraph(uint32_t imageIndex);

    void collectRenderables(entt::registry& world);
    void updateGlobalUniforms(entt::registry& world, uint32_t frameIndex);
    void renderScene(vk::CommandBuffer commandBuffer, uint32_t frameIndex, entt::registry& world);

    // Helper to find active camera (moved from GlobalUniforms)
    Camera* findActiveCamera(entt::registry& world);

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
    const AABB& getSceneBounds() const { return sceneBVH.getSceneBounds(); }

    // Debug rendering
    DebugRenderer& getDebugRenderer() { return debugRenderer; }

    // Statistics access
    const RenderStats& getRenderStats() const { return renderStats; }

    // Scene state management
    void markSceneDirty() { sceneDirty = true; }

    // Swapchain resize
    void resize(vk::Extent2D newExtent);

    // Swapchain access (for RenderGraph to import swapchain images)
    void setSwapchain(class Swapchain* swapchain) { this->swapchain = swapchain; }

    // Skybox access
    EnvironmentMap& getEnvironmentMap() { return environmentMap; }

    // Auto-exposure access
    AutoExposure& getAutoExposure() { return autoExposure; }

    // Tonemap access
    Tonemap& getTonemap() { return tonemap; }

    // PBR Bindless Material access (shared material for all PBR instances)
    Material* getPBRBindlessMaterial() const {
        auto* matMgr = getMaterialManager();
        return matMgr ? matMgr->getMaterialByName("PBRBindless") : nullptr;
    }

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

    // Global uniform using reflection-based API (replaces GlobalUniforms class)
    UniformHandle globalUniformHandle = 0;

    DebugRenderer debugRenderer;

    EnvironmentMap environmentMap;
    AutoExposure autoExposure;
    Tonemap tonemap;

    class LightingSystem* lightingSystem = nullptr;
    class ShadowSystem* shadowSystem = nullptr;
    eastl::unique_ptr<class ShadowPass> shadowPass;

    // Render graph for automatic resource and barrier management
    eastl::unique_ptr<RenderGraph> renderGraph;
    vk::Format swapchainFormat = vk::Format::eB8G8R8A8Srgb;  // Store swapchain format for graph rebuild

    ResourceManager* resourceManager = nullptr;
    class Swapchain* swapchain = nullptr;  // For RenderGraph swapchain image import

};

} // namespace violet