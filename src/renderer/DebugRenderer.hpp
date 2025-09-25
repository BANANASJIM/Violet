#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/array.h>

#include "renderer/Vertex.hpp"
#include "renderer/GPUResource.hpp"
#include "renderer/Pipeline.hpp"
#include "math/AABB.hpp"
#include "math/Frustum.hpp"

namespace violet {

class VulkanContext;
class RenderPass;
class Material;
class MaterialInstance;
class Mesh;
class Pipeline;
class DescriptorSet;
class UniformBuffer;
class GlobalUniforms;

struct DebugColors {
    static constexpr glm::vec3 FRUSTUM = glm::vec3(0.0f, 1.0f, 0.0f);        // Green
    static constexpr glm::vec3 VISIBLE_AABB = glm::vec3(1.0f, 1.0f, 0.0f);   // Yellow
    static constexpr glm::vec3 CULLED_AABB = glm::vec3(1.0f, 0.0f, 0.0f);    // Red
};

class DebugRenderer {
public:
    DebugRenderer() = default;
    ~DebugRenderer();

    DebugRenderer(const DebugRenderer&) = delete;
    DebugRenderer& operator=(const DebugRenderer&) = delete;

    void init(VulkanContext* context, RenderPass* renderPass, GlobalUniforms* globalUniforms, uint32_t maxFramesInFlight);
    void cleanup();

    void renderFrustum(vk::CommandBuffer commandBuffer, uint32_t frameIndex, const Frustum& frustum);
    void renderAABB(vk::CommandBuffer commandBuffer, uint32_t frameIndex, const AABB& aabb, bool isVisible);
    void renderAABBs(vk::CommandBuffer commandBuffer, uint32_t frameIndex, const eastl::vector<AABB>& aabbs, const eastl::vector<bool>& visibilityMask);

    bool isEnabled() const { return enabled; }
    void setEnabled(bool enable) { enabled = enable; }

    bool showFrustum() const { return showFrustumDebug; }
    void setShowFrustum(bool show) { showFrustumDebug = show; }

    bool showAABBs() const { return showAABBDebug; }
    void setShowAABBs(bool show) { showAABBDebug = show; }

private:
    void generateFrustumGeometry(const Frustum& frustum, eastl::vector<Vertex>& vertices, eastl::vector<uint32_t>& indices);
    void generateAABBGeometry(const AABB& aabb, const glm::vec3& color, eastl::vector<Vertex>& vertices, eastl::vector<uint32_t>& indices, uint32_t baseVertexIndex);

    VulkanContext* context = nullptr;
    RenderPass* renderPass = nullptr;
    GlobalUniforms* globalUniforms = nullptr;
    uint32_t maxFramesInFlight = 0;

    eastl::unique_ptr<Material> debugMaterial;

    bool enabled = false;
    bool showFrustumDebug = false;
    bool showAABBDebug = false;

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


    static constexpr size_t MAX_DEBUG_VERTICES = 10000;
    static constexpr size_t MAX_DEBUG_INDICES = 30000;
};

} // namespace violet