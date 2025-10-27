#include "LightingSystem.hpp"
#include "ecs/Components.hpp"
#include "renderer/camera/Camera.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/DescriptorManager.hpp"
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

    ensureBufferCapacity(INITIAL_CAPACITY);

    // Allocate single descriptor set (will use dynamic offset for per-frame access)
    auto sets = descriptorManager->allocateSets("Lighting", 1);
    descriptorSet = sets[0];

    // Bind the buffer to descriptor set (range = alignedFrameSize for dynamic offset)
    eastl::vector<ResourceBindingDesc> bindings;
    bindings.push_back(ResourceBindingDesc::storageBuffer(
        0, lightBuffer.buffer, 0, alignedFrameSize));
    descriptorManager->updateSet(descriptorSet, bindings);

    violet::Log::info("LightingSystem", "Initialized (capacity: {}, aligned frame size: {} bytes)",
                     INITIAL_CAPACITY, alignedFrameSize);
}

void LightingSystem::cleanup() {
    if (!context) return;

    if (lightBuffer.buffer) {
        ResourceFactory::destroyBuffer(context, lightBuffer);
    }

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

    if (!lightBuffer.mappedData) {
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

    // Write to the frame-specific section using dynamic offset
    char* bufferPtr = static_cast<char*>(lightBuffer.mappedData) + (frameIndex * alignedFrameSize);
    memcpy(bufferPtr, &header, sizeof(LightDataHeader));

    if (!cpuLightData.empty()) {
        size_t dataSize = cpuLightData.size() * sizeof(LightData);
        memcpy(bufferPtr + sizeof(LightDataHeader), cpuLightData.data(), dataSize);
    }
}

vk::DescriptorSet LightingSystem::getDescriptorSet(uint32_t frameIndex) const {
    // Return the single descriptor set (caller must provide dynamic offset)
    return descriptorSet;
}

void LightingSystem::ensureBufferCapacity(uint32_t lightCount) {
    if (lightCount <= bufferCapacity) return;

    uint32_t newCapacity = eastl::max(lightCount, bufferCapacity * 2);
    newCapacity = eastl::min(newCapacity, MAX_LIGHTS);

    // Calculate size for one frame's data (header + lights)
    size_t frameDataSize = 16 + newCapacity * sizeof(LightData);

    // Align to minStorageBufferOffsetAlignment for dynamic offset
    vk::PhysicalDeviceProperties props = context->getPhysicalDevice().getProperties();
    uint32_t minAlignment = static_cast<uint32_t>(props.limits.minStorageBufferOffsetAlignment);
    alignedFrameSize = (frameDataSize + minAlignment - 1) & ~(minAlignment - 1);

    // Total buffer size for all frames
    size_t totalBufferSize = alignedFrameSize * maxFramesInFlight;

    // Destroy old buffer if exists
    if (lightBuffer.buffer) {
        ResourceFactory::destroyBuffer(context, lightBuffer);
    }

    // Create single buffer for all frames
    BufferInfo bufferInfo{
        .size = totalBufferSize,
        .usage = vk::BufferUsageFlagBits::eStorageBuffer,
        .memoryUsage = MemoryUsage::CPU_TO_GPU,
        .debugName = "LightDataBuffer_AllFrames"
    };
    lightBuffer = ResourceFactory::createBuffer(context, bufferInfo);

    if (!lightBuffer.mappedData) {
        violet::Log::error("LightingSystem", "Failed to map light buffer");
        return;
    }

    bufferCapacity = newCapacity;

    violet::Log::debug("LightingSystem", "Resized buffer: capacity={}, alignedFrameSize={}, totalSize={}",
                      newCapacity, alignedFrameSize, totalBufferSize);
}

} // namespace violet