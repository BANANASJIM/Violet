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
#include "renderer/Material.hpp"
#include "renderer/Renderable.hpp"
#include "renderer/Texture.hpp"
#include "renderer/UniformBuffer.hpp"
#include "renderer/DebugRenderer.hpp"
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

struct PushConstantData {
    glm::mat4 model;
};

class GlobalUniforms {
public:
    ~GlobalUniforms();
    void           init(VulkanContext* context, uint32_t maxFramesInFlight);
    void           cleanup();
    void           update(entt::registry& world, uint32_t frameIndex);
    DescriptorSet* getDescriptorSet() const { return descriptorSet.get(); }
    Camera* findActiveCamera(entt::registry& world);  // Made public for frustum culling

private:

    VulkanContext*                                  context = nullptr;
    eastl::unique_ptr<DescriptorSet>                descriptorSet;
    eastl::vector<eastl::unique_ptr<UniformBuffer>> uniformBuffers;
    struct GlobalUBO {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 proj;
        alignas(16) glm::vec3 cameraPos;
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

    void init(VulkanContext* context, RenderPass* renderPass, uint32_t maxFramesInFlight) override;
    void cleanup() override;
    void render(vk::CommandBuffer commandBuffer, uint32_t frameIndex) override;

    void collectRenderables(entt::registry& world);
    void updateGlobalUniforms(entt::registry& world, uint32_t frameIndex);
    void renderScene(vk::CommandBuffer commandBuffer, uint32_t frameIndex, entt::registry& world);

    DescriptorSet* getGlobalDescriptorSet() const { return globalUniforms.getDescriptorSet(); }

    Material* createMaterial(const eastl::string& vertexShader, const eastl::string& fragmentShader);
    Material* createMaterial(
        const eastl::string& vertexShader,
        const eastl::string& fragmentShader,
        DescriptorSetType    materialType
    );
    MaterialInstance* createMaterialInstance(Material* material);
    MaterialInstance* createPBRMaterialInstance(Material* material);
    MaterialInstance* createUnlitMaterialInstance(Material* material);

    // Global material instance management
    void              registerMaterialInstance(uint32_t index, MaterialInstance* instance);
    MaterialInstance* getMaterialInstanceByIndex(uint32_t index) const;

    // Texture management for scene loading
    Texture* addTexture(eastl::unique_ptr<Texture> texture);

    void                             clearRenderables() { renderables.clear(); }
    const eastl::vector<Renderable>& getRenderables() const { return renderables; }

    // BVH management
    void buildSceneBVH(entt::registry& world);

    // Debug rendering
    DebugRenderer& getDebugRenderer() { return debugRenderer; }

    // Statistics access
    const RenderStats& getRenderStats() const { return renderStats; }

private:
    void collectFromEntity(entt::entity entity, entt::registry& world);

    GlobalUniforms globalUniforms;

    eastl::vector<Renderable>                          renderables;
    eastl::vector<AABB>                                renderableBounds;  // Bounds for each renderable
    eastl::vector<eastl::unique_ptr<Material>>         materials;
    eastl::vector<eastl::unique_ptr<MaterialInstance>> materialInstances;
    eastl::vector<eastl::unique_ptr<Texture>>          textures;

    // Global material instance index (GLTF material index -> MaterialInstance pointer)
    eastl::hash_map<uint32_t, MaterialInstance*> globalMaterialIndex;

    eastl::hash_map<entt::entity, eastl::vector<uint32_t>> renderableCache;

    // BVH for frustum culling
    BVH sceneBVH;
    eastl::vector<uint32_t> visibleIndices;
    bool sceneDirty = true;    // Track if any objects moved
    bool bvhBuilt = false;     // Track if BVH built at least once

    // Rendering statistics
    RenderStats renderStats;

    // Debug rendering
    DebugRenderer debugRenderer;
};

} // namespace violet