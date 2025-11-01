#include "LightingSystem.hpp"
#include "ecs/Components.hpp"
#include "renderer/camera/Camera.hpp"
#include "renderer/vulkan/ShaderResources.hpp"
#include "renderer/vulkan/ShaderResourceBinding.hpp"
#include "renderer/vulkan/PipelineBase.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/MaterialManager.hpp"
#include "resource/Material.hpp"
#include "core/Log.hpp"

namespace violet {

void LightingSystem::init(ResourceManager* resMgr) {
    if (!resMgr) {
        Log::error("LightingSystem", "Invalid ResourceManager");
        return;
    }

    // Get PBR material and pipeline from ResourceManager
    auto* matMgr = resMgr->getMaterialManager();
    auto* pbrMaterial = matMgr->getMaterialByName("PBRBindless");
    if (!pbrMaterial) {
        Log::error("LightingSystem", "PBRBindless material not found");
        return;
    }

    auto* pipeline = pbrMaterial->getPipeline();
    if (!pipeline) {
        Log::error("LightingSystem", "PBRBindless pipeline not found");
        return;
    }

    // Create ShaderResources from pipeline reflection
    lightsResources = resMgr->createShaderResources(
        "Lights", pipeline, "lights", UpdateFrequency::PerFrame);

    if (!lightsResources) {
        Log::error("LightingSystem", "Failed to create lightsResources");
        return;
    }

    auto bindings = lightsResources->getBufferBindings();
    for (const auto& [key, handle] : bindings) {
        lightsSRB.bind(key.set, key.binding, handle);
    }

    cpuLightData.reserve(64);
    violet::Log::info("LightingSystem", "Initialized with own ShaderResources");
}

void LightingSystem::update(entt::registry& world, const Frustum& cameraFrustum) {
    cpuLightData.clear();
    collectLights(world, cameraFrustum);

    // Upload light data via ShaderResources reflection API
    if (!lightsResources) {
        Log::error("LightingSystem", "lightsResources is null");
        return;
    }

    // Upload each light's data
    for (size_t i = 0; i < cpuLightData.size(); ++i) {
        const auto& light = cpuLightData[i];
        auto lightProxy = (*lightsResources)["lights"][i];

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