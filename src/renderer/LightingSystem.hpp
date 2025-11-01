#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <EASTL/vector.h>
#include <EASTL/shared_ptr.h>
#include <entt/entt.hpp>
#include "renderer/vulkan/ShaderResourceBinding.hpp"

namespace violet {

class Frustum;
class ShaderResources;
class ResourceManager;
class PipelineBase;

// GPU light data (must match shader LightData in TypeDefinitions.slang)
struct LightData {
    glm::vec4 positionAndType;  // xyz=position/direction, w=type (0=dir, 1=point)
    glm::vec4 colorAndRadius;   // xyz=color*intensity (lux/lumens), w=radius
    int32_t shadowIndex;         // Index into ShadowData (-1 if no shadow)
};

class LightingSystem {
public:
    LightingSystem() = default;
    ~LightingSystem() = default;

    LightingSystem(const LightingSystem&) = delete;
    LightingSystem& operator=(const LightingSystem&) = delete;

    void init(ResourceManager* resMgr);
    void update(entt::registry& world, const Frustum& cameraFrustum);

    uint32_t getLightCount() const { return static_cast<uint32_t>(cpuLightData.size()); }
    eastl::vector<LightData>& getLightData() { return cpuLightData; }
    const eastl::vector<LightData>& getLightData() const { return cpuLightData; }

    // Get SRB for merging into ForwardRenderer's globalSRB
    const ShaderResourceBinding& getSRB() const { return lightsSRB; }

private:
    void collectLights(entt::registry& world, const Frustum& cameraFrustum);

private:
    eastl::shared_ptr<ShaderResources> lightsResources;
    ShaderResourceBinding lightsSRB;
    eastl::vector<LightData> cpuLightData;

    static constexpr uint32_t MAX_LIGHTS = 256;
};

} // namespace violet