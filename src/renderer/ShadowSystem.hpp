#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <entt/entt.hpp>
#include "resource/gpu/ResourceFactory.hpp"
#include "resource/TextureManager.hpp"
#include "math/AABB.hpp"
#include "renderer/Renderable.hpp"

namespace violet {

class VulkanContext;
class DescriptorManager;
class DescriptorSet;
class LightingSystem;
class RenderGraph;
class TextureManager;

// GPU shadow data (must match shader)
struct ShadowData {
    // Cascaded Shadow Maps data (for directional lights)
    alignas(16) glm::mat4 cascadeViewProjMatrices[4];  // Light space matrices for each cascade
    alignas(16) glm::vec4 cascadeSplitDepths;          // View space split depths (x,y,z,w for cascades 0-3)
    alignas(16) glm::vec4 atlasRects[4];               // Atlas rects for each cascade (x,y,width,height normalized)

    // Common shadow parameters
    alignas(16) glm::vec4 shadowParams;                // x=bias, y=normalBias, z=blendRange, w=unused
    alignas(4) uint32_t lightType;                     // 0=directional, 1=point
    alignas(4) uint32_t cascadeCount;                  // Number of active cascades (1-4)
    alignas(4) uint32_t atlasIndex;                    // Bindless shadow atlas texture index
    alignas(4) uint32_t padding0;

    // Point light cubemap data (for point lights only)
    alignas(16) glm::mat4 cubeFaceMatrices[6];         // 6 cube face view-proj matrices
    alignas(4) uint32_t padding1[2];
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
    ~ShadowSystem();

    ShadowSystem(const ShadowSystem&) = delete;
    ShadowSystem& operator=(const ShadowSystem&) = delete;

    void init(VulkanContext* context, DescriptorManager* descMgr, TextureManager* texMgr, uint32_t maxFramesInFlight);
    void cleanup();

    void update(entt::registry& world, LightingSystem& lightingSystem, class Camera* camera, uint32_t frameIndex, const AABB& sceneBounds = AABB());
    void uploadToGPU(uint32_t frameIndex);

    vk::DescriptorSet getDescriptorSet(uint32_t frameIndex) const;
    uint32_t getShadowCount() const { return static_cast<uint32_t>(cpuShadowData.size()); }
    uint32_t getAtlasIndex() const { return atlasBindlessIndex; }
    uint32_t getAtlasSize() const { return atlasSize; }

    const eastl::vector<ShadowData>& getShadowData() const { return cpuShadowData; }
    const ImageResource* getAtlasImage() const;  // Get from TextureManager

    // Get shadow renderables (culled for shadow frustum, not camera frustum)
    const eastl::vector<Renderable>& getShadowRenderables() const { return shadowRenderables; }

    // Atlas management
    ShadowAtlasAllocation allocateSpace(uint32_t resolution, uint32_t lightIndex);
    void freeSpace(const ShadowAtlasAllocation& alloc);
    void clearAllAllocations();

private:
    void ensureBufferCapacity(uint32_t shadowCount);
    void createAtlas();

private:
    VulkanContext* context = nullptr;
    DescriptorManager* descriptorManager = nullptr;
    TextureManager* textureManager = nullptr;
    uint32_t maxFramesInFlight = 3;

    eastl::vector<ShadowData> cpuShadowData;
    eastl::vector<class BufferResource> shadowBuffers;
    eastl::unique_ptr<DescriptorSet> descriptorSet;

    // Shadow renderables (all objects that can cast shadows)
    eastl::vector<Renderable> shadowRenderables;

    // Shadow atlas - managed by TextureManager
    struct TextureHandle atlasTextureHandle;
    uint32_t atlasBindlessIndex = 0;
    uint32_t atlasSize = 8192;  // Increased from 4096 for larger shadow coverage
    eastl::vector<ShadowAtlasAllocation> allocations;

    uint32_t bufferCapacity = 0;

    static constexpr uint32_t INITIAL_CAPACITY = 32;
    static constexpr uint32_t MAX_SHADOWS = 128;
};

} // namespace violet