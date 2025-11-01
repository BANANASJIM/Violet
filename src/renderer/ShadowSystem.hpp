#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <EASTL/vector.h>
#include <EASTL/shared_ptr.h>
#include <entt/entt.hpp>
#include "resource/TextureManager.hpp"
#include "resource/gpu/ResourceFactory.hpp"
#include "math/AABB.hpp"
#include "renderer/Renderable.hpp"
#include "renderer/vulkan/ShaderResourceBinding.hpp"

namespace violet {

class VulkanContext;
class LightingSystem;
class RenderGraph;
class TextureManager;
class ShaderResources;
class ResourceManager;
class PipelineBase;

// GPU shadow data (must match shader ShadowData in TypeDefinitions.slang)
struct ShadowData {
    // Cascaded Shadow Maps data (for directional lights)
    glm::mat4 cascadeViewProjMatrices[4];  // Light space matrices for each cascade
    glm::vec4 cascadeSplitDepths;          // View space split depths (x,y,z,w for cascades 0-3)
    glm::vec4 atlasRects[4];               // Atlas rects for each cascade (x,y,width,height normalized)

    // Common shadow parameters
    glm::vec4 shadowParams;                // x=bias, y=normalBias, z=blendRange, w=unused
    uint32_t lightType;                     // 0=directional, 1=point
    uint32_t cascadeCount;                  // Number of active cascades (1-4)
    uint32_t atlasIndex;                    // Bindless shadow atlas texture index

    // Point light cubemap data (for point lights only)
    glm::mat4 cubeFaceMatrices[6];         // 6 cube face view-proj matrices
};

struct ShadowAtlasAllocation {
    glm::vec4 rect;            // Normalized UV coords (x, y, width, height)
    uint32_t resolution;       // Actual pixel resolution
    uint32_t lightIndex;       // Index into LightData array
    bool inUse;
};

class ShadowSystem {
public:
    ShadowSystem() = default;
    ~ShadowSystem() = default;

    ShadowSystem(const ShadowSystem&) = delete;
    ShadowSystem& operator=(const ShadowSystem&) = delete;

    void init(VulkanContext* context, ResourceManager* resMgr);
    void cleanup();

    void update(entt::registry& world, LightingSystem& lightingSystem, class Camera* camera, const AABB& sceneBounds = AABB());

    uint32_t getShadowCount() const { return static_cast<uint32_t>(cpuShadowData.size()); }
    uint32_t getAtlasIndex() const { return atlasBindlessIndex; }
    uint32_t getAtlasSize() const { return atlasSize; }

    const eastl::vector<ShadowData>& getShadowData() const { return cpuShadowData; }
    const ImageResource* getAtlasImage() const;  // Get from TextureManager

    // Get shadow renderables (culled for shadow frustum, not camera frustum)
    const eastl::vector<Renderable>& getShadowRenderables() const { return shadowRenderables; }

    // Get SRB for merging into ForwardRenderer's globalSRB
    const ShaderResourceBinding& getSRB() const { return shadowsSRB; }

    // Atlas management
    ShadowAtlasAllocation allocateSpace(uint32_t resolution, uint32_t lightIndex);
    void freeSpace(const ShadowAtlasAllocation& alloc);
    void clearAllAllocations();

private:
    void createAtlas();

private:
    VulkanContext* context = nullptr;
    TextureManager* textureManager = nullptr;
    DescriptorManager* descriptorManager = nullptr;

    eastl::shared_ptr<ShaderResources> shadowsResources;
    ShaderResourceBinding shadowsSRB;
    eastl::vector<ShadowData> cpuShadowData;

    // Shadow renderables (all objects that can cast shadows)
    eastl::vector<Renderable> shadowRenderables;

    // Shadow atlas - managed by TextureManager
    struct TextureHandle atlasTextureHandle;
    uint32_t atlasBindlessIndex = 0;
    uint32_t atlasSize = 8192;  // Increased from 4096 for larger shadow coverage
    eastl::vector<ShadowAtlasAllocation> allocations;

    static constexpr uint32_t MAX_SHADOWS = 128;
};

} // namespace violet