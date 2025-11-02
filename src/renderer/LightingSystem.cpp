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

    cpuLightData.reserve(64);
    violet::Log::info("LightingSystem", "Initialized with own ShaderResources");
}

void LightingSystem::setGlobalResources(eastl::shared_ptr<ShaderResources> resources) {
    globalResources = resources;
}

void LightingSystem::update(entt::registry& world, const Frustum& cameraFrustum) {
    cpuLightData.clear();
    collectLights(world, cameraFrustum);

    violet::Log::info("LightingSystem", "Collected {} lights", cpuLightData.size());

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

        violet::Log::info("LightingSystem", "Light[{}]: type={}, color=({},{},{}), shadowIndex={}",
            i, light.positionAndType.w,
            light.colorAndRadius.x, light.colorAndRadius.y, light.colorAndRadius.z,
            light.shadowIndex);
    }

    if (globalResources) {
        auto camera = (*globalResources)["camera"];
        int numLights = static_cast<int>(cpuLightData.size());
        camera["numLights"] = numLights;
        violet::Log::info("LightingSystem", "Updated numLights to GPU: {}", numLights);
    } else {
        violet::Log::error("LightingSystem", "globalResources is null, cannot update numLights");
    }

    // Update SRB with current frame's buffer bindings
    lightsSRB.clear();
    if (lightsResources) {
        auto bindings = lightsResources->getBufferBindings();
        for (const auto& [key, handle] : bindings) {
            lightsSRB.bind(key.set, key.binding, handle);
        }
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

void LightingSystem::updateLightShadowIndex(uint32_t lightIndex, int32_t shadowIndex) {
    if (lightIndex >= cpuLightData.size()) {
        violet::Log::error("LightingSystem", "Light index {} out of range (max: {})", lightIndex, cpuLightData.size());
        return;
    }

    if (!lightsResources) {
        violet::Log::error("LightingSystem", "lightsResources is null");
        return;
    }

    // Update CPU-side data
    cpuLightData[lightIndex].shadowIndex = shadowIndex;

    // DEBUG: Check current frame
    violet::Log::info("LightingSystem", "updateLightShadowIndex: currentFrame={}, updating Light[{}] shadowIndex to {}",
                     lightsResources->getCurrentFrame(), lightIndex, shadowIndex);

    // Upload to GPU via ShaderResources reflection API (uses current frame)
    auto lightProxy = (*lightsResources)["lights"][lightIndex];
    lightProxy["shadowIndex"] = shadowIndex;
}

// Removed: uploadToGPU, getDescriptorSet, ensureBufferCapacity
// All data upload is now handled directly in update() via ShaderResources API

} // namespace violet