#pragma once

#include "BaseRenderer.hpp"
#include <vulkan/vulkan.hpp>

#include <glm/glm.hpp>

#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>

#include <entt/entt.hpp>

#include "renderer/DescriptorSet.hpp"
#include "renderer/DescriptorManager.hpp"
#include "renderer/Material.hpp"
#include "renderer/Renderable.hpp"
#include "renderer/Texture.hpp"
#include "renderer/UniformBuffer.hpp"
#include "renderer/DebugRenderer.hpp"
#include "renderer/EnvironmentMap.hpp"
#include "renderer/RenderPass.hpp"
#include "acceleration/BVH.hpp"

namespace violet {

// Descriptor Set and Binding 约定常量
constexpr uint32_t GLOBAL_SET   = 0; // set = 0: 全局数据(相机、光照)
constexpr uint32_t MATERIAL_SET = 1; // set = 1: 材质数据(纹理、材质参数)

constexpr uint32_t CAMERA_UBO_BINDING         = 0; // Global set binding 0: 相机变换矩阵
constexpr uint32_t BASE_COLOR_TEXTURE_BINDING = 0; // Material set binding 0: 基础颜色纹理

class Camera;

// Rendering statistics structure
struct RenderStats {
    uint32_t totalRenderables = 0;
    uint32_t visibleRenderables = 0;
    uint32_t drawCalls = 0;
    uint32_t skippedRenderables = 0;
};

// Bindless push constants (includes material ID)
struct BindlessPushConstants {
    glm::mat4 model;
    uint32_t materialID;
    uint32_t padding[3];  // Align to 16 bytes
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

    void init(VulkanContext* context, vk::Format swapchainFormat, uint32_t maxFramesInFlight);

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

    // Modern API: Use DescriptorManager layout names (e.g., "PBRMaterial", "UnlitMaterial", "PostProcess")
    Material* createMaterial(const eastl::string& vertexShader, const eastl::string& fragmentShader);
    Material* createMaterial(
        const eastl::string& vertexShader,
        const eastl::string& fragmentShader,
        const eastl::string& materialLayoutName
    );
    Material* createMaterial(
        const eastl::string& vertexShader,
        const eastl::string& fragmentShader,
        const eastl::string&  materialLayoutName,
        const PipelineConfig& config
    );
    Material* createMaterial(
        const eastl::string& vertexShader,
        const eastl::string& fragmentShader,
        const eastl::string&  materialLayoutName,
        const PipelineConfig& config,
        RenderPass*          renderPass
    );
    MaterialInstance* createMaterialInstance(Material* material);
    MaterialInstance* createPBRMaterialInstance(Material* material);
    MaterialInstance* createUnlitMaterialInstance(Material* material);

    // Global material instance management
    void              registerMaterialInstance(uint32_t index, MaterialInstance* instance);
    MaterialInstance* getMaterialInstanceByIndex(uint32_t index) const;

    // Texture management for scene loading
    Texture* addTexture(eastl::unique_ptr<Texture> texture);

    // Default PBR texture access
    Texture* getDefaultWhiteTexture() const { return defaultWhiteTexture; }
    Texture* getDefaultBlackTexture() const { return defaultBlackTexture; }
    Texture* getDefaultMetallicRoughnessTexture() const { return defaultMetallicRoughnessTexture; }
    Texture* getDefaultNormalTexture() const { return defaultNormalTexture; }

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

    // Multi-pass system
    void setupPasses(vk::Format swapchainFormat);
    const eastl::vector<eastl::unique_ptr<Pass>>& getPasses() const { return passes; }
    Pass* getPass(size_t index) { return passes[index].get(); }
    RenderPass* getRenderPass(size_t index);  // Helper to get RenderPass specifically

    // Skybox access
    EnvironmentMap& getEnvironmentMap() { return environmentMap; }
    GlobalUniforms& getGlobalUniforms() { return globalUniforms; }

    // PostProcess access
    Material* getPostProcessMaterial() const { return postProcessMaterial; }
    void updatePostProcessDescriptors();  // Update descriptor set with offscreen textures

    // Descriptor manager access
    DescriptorManager& getDescriptorManager() { return descriptorManager; }

private:
    void cleanup();  // Private cleanup function called from destructor
    void collectFromEntity(entt::entity entity, entt::registry& world);
    void createDefaultPBRTextures();

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

    Texture* defaultWhiteTexture = nullptr;
    Texture* defaultBlackTexture = nullptr;
    Texture* defaultMetallicRoughnessTexture = nullptr;
    Texture* defaultNormalTexture = nullptr;

    Material* postProcessMaterial = nullptr;
    Material* pbrBindlessMaterial = nullptr;

    vk::Sampler postProcessSampler = VK_NULL_HANDLE;

    eastl::vector<eastl::unique_ptr<Texture>>          textures;

    eastl::vector<eastl::unique_ptr<MaterialInstance>> materialInstances;
    eastl::vector<eastl::unique_ptr<Material>>         materials;
    eastl::unique_ptr<DescriptorSet> postProcessDescriptorSet;
    eastl::hash_map<uint32_t, MaterialInstance*> globalMaterialIndex;

    GlobalUniforms globalUniforms;
    DebugRenderer debugRenderer;
    EnvironmentMap environmentMap;
    eastl::vector<eastl::unique_ptr<Pass>> passes;

    DescriptorManager descriptorManager;

};

} // namespace violet