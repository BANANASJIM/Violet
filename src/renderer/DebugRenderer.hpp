#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/array.h>

#include "BaseRenderer.hpp"
#include "renderer/Vertex.hpp"
#include "renderer/GPUResource.hpp"
#include "renderer/Pipeline.hpp"
#include "math/AABB.hpp"
#include "math/Frustum.hpp"
#include <entt/entt.hpp>

namespace violet {

class Material;
class MaterialInstance;
class Mesh;
class Pipeline;
class DescriptorSet;
class UniformBuffer;
class GlobalUniforms;
class World;
class ForwardRenderer;

struct DebugColors {
    static constexpr glm::vec3 FRUSTUM = glm::vec3(0.0f, 1.0f, 0.0f);        // Green
    static constexpr glm::vec3 VISIBLE_AABB = glm::vec3(1.0f, 1.0f, 0.0f);   // Yellow
    static constexpr glm::vec3 CULLED_AABB = glm::vec3(1.0f, 0.0f, 0.0f);    // Red
    static constexpr glm::vec3 SELECTED_ENTITY = glm::vec3(1.0f, 0.5f, 0.0f); // Orange
    static constexpr glm::vec3 RAY = glm::vec3(0.0f, 1.0f, 1.0f);            // Cyan
};

class DebugRenderer : public BaseRenderer {
public:
    DebugRenderer() = default;
    ~DebugRenderer() override;

    DebugRenderer(const DebugRenderer&) = delete;
    DebugRenderer& operator=(const DebugRenderer&) = delete;

    void init(VulkanContext* ctx, RenderPass* rp, uint32_t framesInFlight) override;
    void init(VulkanContext* context, RenderPass* renderPass, GlobalUniforms* globalUniforms, uint32_t maxFramesInFlight);
    void cleanup() override;
    void render(vk::CommandBuffer commandBuffer, uint32_t frameIndex) override;

    void renderFrustum(vk::CommandBuffer commandBuffer, uint32_t frameIndex, const Frustum& frustum);
    void renderAABB(vk::CommandBuffer commandBuffer, uint32_t frameIndex, const AABB& aabb, bool isVisible);
    void renderAABBs(vk::CommandBuffer commandBuffer, uint32_t frameIndex, const eastl::vector<AABB>& aabbs, const eastl::vector<bool>& visibilityMask);
    void renderRay(vk::CommandBuffer commandBuffer, uint32_t frameIndex, const glm::vec3& origin, const glm::vec3& direction, float length = 1000.0f);

    // Batched ray rendering for multiple rays
    void beginRayBatch();
    void addRayToBatch(const glm::vec3& origin, const glm::vec3& direction, float length);
    void renderRayBatch(vk::CommandBuffer commandBuffer, uint32_t frameIndex);

    // Ray data management for UI integration
    void setRayData(const glm::vec3& origin, const glm::vec3& direction, float length, bool enabled);
    void clearRayData();
    bool hasRayData() const { return rayDataEnabled; }

    // Ray data accessors
    glm::vec3 getRayOrigin() const { return storedRayOrigin; }
    glm::vec3 getRayDirection() const { return storedRayDirection; }
    float getRayLength() const { return storedRayLength; }

    void setSelectedEntity(entt::entity entity) { selectedEntity = entity; }
    entt::entity getSelectedEntity() const { return selectedEntity; }
    void renderSelectedEntity(vk::CommandBuffer commandBuffer, uint32_t frameIndex, entt::registry& world, const ForwardRenderer& renderer);

    bool isEnabled() const { return enabled; }
    void setEnabled(bool enable) { enabled = enable; }

    bool showFrustum() const { return showFrustumDebug; }
    void setShowFrustum(bool show) { showFrustumDebug = show; }

    bool showAABBs() const { return showAABBDebug; }
    void setShowAABBs(bool show) { showAABBDebug = show; }

private:
    void generateFrustumGeometry(const Frustum& frustum, eastl::vector<Vertex>& vertices, eastl::vector<uint32_t>& indices);
    void generateAABBGeometry(const AABB& aabb, const glm::vec3& color, eastl::vector<Vertex>& vertices, eastl::vector<uint32_t>& indices, uint32_t baseVertexIndex);
    void generateWireframeGeometry(const eastl::vector<Vertex>& meshVertices, const eastl::vector<uint32_t>& meshIndices,
                                  const glm::vec3& color, eastl::vector<Vertex>& outVertices, eastl::vector<uint32_t>& outIndices);

    GlobalUniforms* globalUniforms = nullptr;

    eastl::unique_ptr<Material> debugMaterial;

    bool enabled = false;
    bool showFrustumDebug = false;
    bool showAABBDebug = false;

    // Selected entity rendering
    entt::entity selectedEntity = entt::null;

    // Ray data for UI integration
    glm::vec3 storedRayOrigin{0.0f};
    glm::vec3 storedRayDirection{0.0f};
    float storedRayLength = 100.0f;
    bool rayDataEnabled = false;

    // Dynamic geometry buffers for each frame
    struct FrameData {
        eastl::unique_ptr<BufferResource> vertexBuffer;
        eastl::unique_ptr<BufferResource> indexBuffer;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
    };
    eastl::vector<FrameData> frameData;

    // Debug pipeline
    eastl::unique_ptr<Pipeline> debugPipeline;
    // Wireframe pipeline for mesh rendering
    eastl::unique_ptr<Pipeline> wireframePipeline;
    // Solid pipeline for filled mesh rendering
    eastl::unique_ptr<Pipeline> solidPipeline;

    // Batched ray rendering data
    eastl::vector<Vertex> batchedRayVertices;
    eastl::vector<uint32_t> batchedRayIndices;

    static constexpr size_t MAX_DEBUG_VERTICES = 10000;
    static constexpr size_t MAX_DEBUG_INDICES = 30000;
};

} // namespace violet