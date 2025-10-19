#include "LightingSystem.hpp"
#include "ecs/Components.hpp"
#include "renderer/camera/Camera.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/DescriptorManager.hpp"
#include "renderer/vulkan/DescriptorSet.hpp"
#include "resource/gpu/ResourceFactory.hpp"
#include "core/Log.hpp"

namespace violet {

LightingSystem::~LightingSystem() {
    cleanup();
}

void LightingSystem::init(VulkanContext* ctx, DescriptorManager* descMgr, uint32_t framesInFlight) {
    context = ctx;
    descriptorManager = descMgr;
    maxFramesInFlight = framesInFlight;

    cpuLightData.reserve(INITIAL_CAPACITY);
    lightBuffers.resize(maxFramesInFlight);

    ensureBufferCapacity(INITIAL_CAPACITY);

    auto sets = descriptorManager->allocateSets("Lighting", maxFramesInFlight);
    descriptorSet = eastl::make_unique<DescriptorSet>();
    descriptorSet->init(context, sets);

    for (uint32_t i = 0; i < maxFramesInFlight; i++) {
        eastl::vector<ResourceBindingDesc> bindings;
        bindings.push_back(ResourceBindingDesc::storageBuffer(
            0, lightBuffers[i].buffer, 0, bufferCapacity * sizeof(LightData)));
        descriptorManager->updateSet(sets[i], bindings);
    }

    violet::Log::info("LightingSystem", "Initialized (capacity: {})", INITIAL_CAPACITY);
}

void LightingSystem::cleanup() {
    if (!context) return;

    descriptorSet.reset();

    for (auto& buffer : lightBuffers) {
        ResourceFactory::destroyBuffer(context, buffer);
    }

    lightBuffers.clear();
    cpuLightData.clear();

    context = nullptr;
    descriptorManager = nullptr;
}

void LightingSystem::update(entt::registry& world, const Frustum& cameraFrustum, uint32_t frameIndex) {
    cpuLightData.clear();
    collectLights(world, cameraFrustum);

    if (!cpuLightData.empty()) {
        ensureBufferCapacity(static_cast<uint32_t>(cpuLightData.size()));
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

void LightingSystem::uploadToGPU(uint32_t frameIndex) {
    if (frameIndex >= maxFramesInFlight) {
        return;
    }

    if (!lightBuffers[frameIndex].mappedData) {
        violet::Log::error("LightingSystem", "Buffer not mapped");
        return;
    }

    struct LightDataHeader {
        uint32_t count;
        uint32_t padding[3];
    };

    LightDataHeader header;
    header.count = static_cast<uint32_t>(cpuLightData.size());
    header.padding[0] = 0;
    header.padding[1] = 0;
    header.padding[2] = 0;

    char* bufferPtr = static_cast<char*>(lightBuffers[frameIndex].mappedData);
    memcpy(bufferPtr, &header, sizeof(LightDataHeader));

    if (!cpuLightData.empty()) {
        size_t dataSize = cpuLightData.size() * sizeof(LightData);
        memcpy(bufferPtr + sizeof(LightDataHeader), cpuLightData.data(), dataSize);
    }
}

vk::DescriptorSet LightingSystem::getDescriptorSet(uint32_t frameIndex) const {
    return descriptorSet->getDescriptorSet(frameIndex);
}

void LightingSystem::ensureBufferCapacity(uint32_t lightCount) {
    if (lightCount <= bufferCapacity) return;

    uint32_t newCapacity = eastl::max(lightCount, bufferCapacity * 2);
    newCapacity = eastl::min(newCapacity, MAX_LIGHTS);

    auto sets = descriptorManager->allocateSets("Lighting", maxFramesInFlight);

    for (uint32_t i = 0; i < maxFramesInFlight; i++) {
        ResourceFactory::destroyBuffer(context, lightBuffers[i]);

        size_t bufferSize = 16 + newCapacity * sizeof(LightData);

        BufferInfo bufferInfo{
            .size = bufferSize,
            .usage = vk::BufferUsageFlagBits::eStorageBuffer,
            .memoryUsage = MemoryUsage::CPU_TO_GPU,
            .debugName = "LightDataBuffer"
        };
        lightBuffers[i] = ResourceFactory::createBuffer(context, bufferInfo);

        eastl::vector<ResourceBindingDesc> bindings;
        bindings.push_back(ResourceBindingDesc::storageBuffer(
            0, lightBuffers[i].buffer, 0, bufferSize));
        descriptorManager->updateSet(sets[i], bindings);
    }

    bufferCapacity = newCapacity;
}

} // namespace violet