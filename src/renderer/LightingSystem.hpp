#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <entt/entt.hpp>
#include "resource/gpu/ResourceFactory.hpp"

namespace violet {

class VulkanContext;
class DescriptorManager;
class Frustum;

// GPU light data (must match shader)
struct LightData {
    alignas(16) glm::vec4 positionAndType;  // xyz=position/direction, w=type (0=dir, 1=point)
    alignas(16) glm::vec4 colorAndRadius;   // xyz=color*intensity (lux/lumens), w=radius
    alignas(4) int32_t shadowIndex;         // Index into ShadowData (-1 if no shadow)
    alignas(4) uint32_t padding[3];
};

class LightingSystem {
public:
    LightingSystem() = default;
    ~LightingSystem();

    LightingSystem(const LightingSystem&) = delete;
    LightingSystem& operator=(const LightingSystem&) = delete;

    void init(VulkanContext* context, DescriptorManager* descMgr, uint32_t maxFramesInFlight);
    void cleanup();

    void update(entt::registry& world, const Frustum& cameraFrustum, uint32_t frameIndex);
    void uploadToGPU(uint32_t frameIndex);

    vk::DescriptorSet getDescriptorSet(uint32_t frameIndex) const;
    uint32_t getLightCount() const { return static_cast<uint32_t>(cpuLightData.size()); }

    eastl::vector<LightData>& getLightData() { return cpuLightData; }
    const eastl::vector<LightData>& getLightData() const { return cpuLightData; }

private:
    void collectLights(entt::registry& world, const Frustum& cameraFrustum);
    void ensureBufferCapacity(uint32_t lightCount);

private:
    VulkanContext* context = nullptr;
    DescriptorManager* descriptorManager = nullptr;
    uint32_t maxFramesInFlight = 3;

    eastl::vector<LightData> cpuLightData;
    class BufferResource lightBuffer;          // Single buffer with per-frame sections
    vk::DescriptorSet descriptorSet;          // Single descriptor set with dynamic offset
    uint32_t alignedFrameSize = 0;             // Aligned size for each frame's data

    uint32_t bufferCapacity = 0;

    static constexpr uint32_t INITIAL_CAPACITY = 64;
    static constexpr uint32_t MAX_LIGHTS = 256;
};

} // namespace violet