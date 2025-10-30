#include "LightingSystem.hpp"
#include "ecs/Components.hpp"
#include "renderer/camera/Camera.hpp"
#include "renderer/vulkan/ShaderResources.hpp"
#include "core/Log.hpp"

namespace violet {

void LightingSystem::init(eastl::shared_ptr<ShaderResources> globalRes) {
    globalResources = globalRes;
    cpuLightData.reserve(64);
    violet::Log::info("LightingSystem", "Initialized with ShaderResources reflection API");
}

void LightingSystem::update(entt::registry& world, const Frustum& cameraFrustum, uint32_t frameIndex) {
    cpuLightData.clear();
    collectLights(world, cameraFrustum);

    // Upload light data via ShaderResources reflection API
    if (!globalResources) {
        Log::error("LightingSystem", "globalResources is null");
        return;
    }

    // Update light count in camera UBO
    (*globalResources)["camera"]["numLights"] = static_cast<int>(cpuLightData.size());

    // Upload each light's data
    for (size_t i = 0; i < cpuLightData.size(); ++i) {
        const auto& light = cpuLightData[i];
        auto lightProxy = (*globalResources)["lights"][i];

        lightProxy["positionAndType"] = light.positionAndType;
        lightProxy["colorAndRadius"] = light.colorAndRadius;
        lightProxy["shadowIndex"] = light.shadowIndex;
    }
}

void LightingSystem::collectLights(entt::registry& world, const Frustum& cameraFrustum) {
    auto lightView = world.view<LightComponent, TransformComponent>();

    for (auto entity : lightView) {
        const auto& light = lightView.get<LightComponent>(entity);
        const auto& transform = lightView.get<TransformComponent>(entity);

        if (!light.enabled) continue;

        // Frustum culling for point lights
        if (light.type == LightType::Point) {
            AABB lightBounds = light.getBoundingSphere(transform.world.position);
            if (!cameraFrustum.testAABB(lightBounds)) {
                continue;
            }
        }

        // Build LightData
        LightData lightData;

        if (light.type == LightType::Directional) {
            lightData.positionAndType = glm::vec4(light.direction, 0.0f);
        } else {
            lightData.positionAndType = glm::vec4(transform.world.position, 1.0f);
        }

        glm::vec3 finalColor = light.color * light.intensity;
        lightData.colorAndRadius = glm::vec4(finalColor, light.radius);
        lightData.shadowIndex = -1;  // Will be set by ShadowPass if needed

        cpuLightData.push_back(lightData);

        // Check if we hit the limit
        if (cpuLightData.size() >= MAX_LIGHTS) {
            violet::Log::warn("LightingSystem", "Reached MAX_LIGHTS ({}), ignoring remaining lights", MAX_LIGHTS);
            break;
        }
    }
}

// Removed: uploadToGPU, getDescriptorSet, ensureBufferCapacity
// All data upload is now handled directly in update() via ShaderResources API

} // namespace violet