#include "ShadowSystem.hpp"
#include "LightingSystem.hpp"
#include "ecs/Components.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/DescriptorManager.hpp"
#include "renderer/vulkan/DescriptorSet.hpp"
#include "renderer/camera/Camera.hpp"
#include "resource/gpu/ResourceFactory.hpp"
#include "resource/TextureManager.hpp"
#include "resource/Texture.hpp"
#include "core/Log.hpp"
#include <cmath>

namespace {
    // Helper function to compute frustum corners in world space
    eastl::array<glm::vec3, 8> getFrustumCornersWorldSpace(const glm::mat4& proj, const glm::mat4& view) {
        const glm::mat4 inv = glm::inverse(proj * view);

        eastl::array<glm::vec3, 8> frustumCorners;
        uint32_t i = 0;

        // 8 corners of NDC cube: (-1,-1,-1) to (1,1,1) for Vulkan (Z: [0,1])
        for (uint32_t x = 0; x < 2; ++x) {
            for (uint32_t y = 0; y < 2; ++y) {
                for (uint32_t z = 0; z < 2; ++z) {
                    const glm::vec4 pt = inv * glm::vec4(
                        2.0f * x - 1.0f,  // -1 or 1
                        2.0f * y - 1.0f,  // -1 or 1
                        z,                // 0 or 1 (Vulkan depth range)
                        1.0f
                    );
                    frustumCorners[i++] = glm::vec3(pt) / pt.w;
                }
            }
        }

        return frustumCorners;
    }
}

namespace violet {

ShadowSystem::~ShadowSystem() {
    cleanup();
}

void ShadowSystem::init(VulkanContext* ctx, DescriptorManager* descMgr, TextureManager* texMgr, uint32_t framesInFlight) {
    context = ctx;
    descriptorManager = descMgr;
    textureManager = texMgr;
    maxFramesInFlight = framesInFlight;

    cpuShadowData.reserve(INITIAL_CAPACITY);
    shadowBuffers.resize(maxFramesInFlight);

    ensureBufferCapacity(INITIAL_CAPACITY);

    auto sets = descriptorManager->allocateSets("Shadow", maxFramesInFlight);
    descriptorSet = eastl::make_unique<DescriptorSet>();
    descriptorSet->init(context, sets);

    for (uint32_t i = 0; i < maxFramesInFlight; i++) {
        eastl::vector<ResourceBindingDesc> bindings;
        bindings.push_back(ResourceBindingDesc::storageBuffer(
            0, shadowBuffers[i].buffer, 0, bufferCapacity * sizeof(ShadowData)));
        descriptorManager->updateSet(sets[i], bindings);
    }

    createAtlas();

    violet::Log::info("ShadowSystem", "Initialized (atlas: {}x{}, capacity: {})",
                      atlasSize, atlasSize, INITIAL_CAPACITY);
}

void ShadowSystem::cleanup() {
    if (!context) return;

    descriptorSet.reset();

    for (auto& buffer : shadowBuffers) {
        ResourceFactory::destroyBuffer(context, buffer);
    }

    // Remove shadow atlas texture from TextureManager
    if (atlasTextureHandle.isValid() && textureManager) {
        textureManager->removeTexture(atlasTextureHandle);
        atlasTextureHandle = TextureHandle{};
    }

    shadowBuffers.clear();
    cpuShadowData.clear();
    allocations.clear();

    context = nullptr;
    descriptorManager = nullptr;
    textureManager = nullptr;
}

void ShadowSystem::update(entt::registry& world, LightingSystem& lightingSystem, Camera* camera, uint32_t frameIndex) {
    cpuShadowData.clear();
    clearAllAllocations();

    if (!camera) {
        violet::Log::warn("ShadowSystem", "No active camera, skipping shadow update");
        return;
    }

    auto& lightData = lightingSystem.getLightData();

    // Get camera matrices for frustum calculation
    const glm::mat4 view = camera->getViewMatrix();
    const glm::mat4 proj = camera->getProjectionMatrix();

    // Collect shadow-casting lights
    uint32_t lightIndex = 0;
    auto lightView = world.view<LightComponent, TransformComponent>();

    for (auto entity : lightView) {
        const auto& light = lightView.get<LightComponent>(entity);
        const auto& transform = lightView.get<TransformComponent>(entity);

        if (!light.enabled || !light.castsShadows) {
            lightIndex++;
            continue;
        }

        if (lightIndex >= lightData.size()) break;

        // Allocate atlas space
        uint32_t resolution = light.shadowResolution;
        auto alloc = allocateSpace(resolution, lightIndex);

        if (!alloc.inUse) {
            lightIndex++;
            continue; // Atlas full
        }

        // Build shadow data
        ShadowData shadowData{};
        shadowData.atlasRect = alloc.rect;
        shadowData.shadowParams = glm::vec4(light.shadowBias, light.shadowNormalBias, 0.0f, 0.0f);
        shadowData.lightType = (light.type == LightType::Directional) ? 0 : 1;
        shadowData.atlasIndex = atlasBindlessIndex;

        // Compute light space matrices
        if (light.type == LightType::Directional) {
            glm::vec3 lightDir = glm::normalize(light.direction);
            glm::vec3 up = (std::abs(lightDir.y) < 0.999f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);

            // Calculate frustum corners in world space
            auto frustumCorners = getFrustumCornersWorldSpace(proj, view);

            // Compute frustum center
            glm::vec3 center = glm::vec3(0.0f);
            for (const auto& corner : frustumCorners) {
                center += corner;
            }
            center /= frustumCorners.size();

            // Build light view matrix looking at frustum center
            glm::mat4 lightViewMatrix = glm::lookAt(
                center - lightDir * light.shadowFarPlane,  // Position light behind frustum
                center,                                     // Look at frustum center
                up
            );

            // Transform frustum corners to light space
            float minX = std::numeric_limits<float>::max();
            float maxX = std::numeric_limits<float>::lowest();
            float minY = std::numeric_limits<float>::max();
            float maxY = std::numeric_limits<float>::lowest();
            float minZ = std::numeric_limits<float>::max();
            float maxZ = std::numeric_limits<float>::lowest();

            for (const auto& corner : frustumCorners) {
                const glm::vec4 trf = lightViewMatrix * glm::vec4(corner, 1.0f);
                minX = std::min(minX, trf.x);
                maxX = std::max(maxX, trf.x);
                minY = std::min(minY, trf.y);
                maxY = std::max(maxY, trf.y);
                minZ = std::min(minZ, trf.z);
                maxZ = std::max(maxZ, trf.z);
            }

            // Use exact frustum bounds without extension
            // Build tight orthographic projection matching frustum exactly
            glm::mat4 lightProjMatrix = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);
            shadowData.lightSpaceMatrix = lightProjMatrix * lightViewMatrix;

            // Debug logging (disabled for performance)
            // violet::Log::info("ShadowSystem", "Light {}: dir=({:.2f},{:.2f},{:.2f}), center=({:.2f},{:.2f},{:.2f}), ortho=[{:.2f},{:.2f}]x[{:.2f},{:.2f}]x[{:.2f},{:.2f}]",
            //                  lightIndex, lightDir.x, lightDir.y, lightDir.z,
            //                  center.x, center.y, center.z,
            //                  minX, maxX, minY, maxY, minZ, maxZ);
        } else if (light.type == LightType::Point) {
            // Point light: 6 cube face matrices
            glm::vec3 lightPos = transform.world.position;
            float nearPlane = light.shadowNearPlane;
            float farPlane = light.shadowFarPlane;

            glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, nearPlane, farPlane);

            glm::vec3 directions[6] = {
                {1, 0, 0}, {-1, 0, 0},
                {0, 1, 0}, {0, -1, 0},
                {0, 0, 1}, {0, 0, -1}
            };
            glm::vec3 ups[6] = {
                {0, -1, 0}, {0, -1, 0},
                {0, 0, 1}, {0, 0, -1},
                {0, -1, 0}, {0, -1, 0}
            };

            for (int i = 0; i < 6; i++) {
                glm::mat4 view = glm::lookAt(lightPos, lightPos + directions[i], ups[i]);
                shadowData.cubeFaceMatrices[i] = projection * view;
            }
        }

        uint32_t shadowIndex = static_cast<uint32_t>(cpuShadowData.size());
        cpuShadowData.push_back(shadowData);

        // Update light's shadow index
        lightingSystem.getLightData()[lightIndex].shadowIndex = static_cast<int32_t>(shadowIndex);

        lightIndex++;

        if (cpuShadowData.size() >= MAX_SHADOWS) {
            violet::Log::warn("ShadowSystem", "Reached MAX_SHADOWS ({})", MAX_SHADOWS);
            break;
        }
    }

    if (!cpuShadowData.empty()) {
        ensureBufferCapacity(static_cast<uint32_t>(cpuShadowData.size()));
    }
}

void ShadowSystem::uploadToGPU(uint32_t frameIndex) {
    if (frameIndex >= maxFramesInFlight || cpuShadowData.empty()) {
        return;
    }

    if (!shadowBuffers[frameIndex].mappedData) {
        violet::Log::error("ShadowSystem", "Buffer not mapped");
        return;
    }

    size_t dataSize = cpuShadowData.size() * sizeof(ShadowData);
    memcpy(shadowBuffers[frameIndex].mappedData, cpuShadowData.data(), dataSize);
}

vk::DescriptorSet ShadowSystem::getDescriptorSet(uint32_t frameIndex) const {
    return descriptorSet->getDescriptorSet(frameIndex);
}

ShadowAtlasAllocation ShadowSystem::allocateSpace(uint32_t resolution, uint32_t lightIndex) {
    // Simple linear packing algorithm
    // TODO: Implement proper rectangle packing (shelf algorithm or guillotine)

    uint32_t currentX = 0;
    uint32_t currentY = 0;
    uint32_t rowHeight = 0;

    for (const auto& existing : allocations) {
        if (!existing.inUse) continue;

        uint32_t allocWidth = static_cast<uint32_t>(existing.rect.z * atlasSize);
        uint32_t allocHeight = static_cast<uint32_t>(existing.rect.w * atlasSize);

        if (currentX + allocWidth <= atlasSize) {
            currentX += allocWidth;
            rowHeight = eastl::max(rowHeight, allocHeight);
        } else {
            currentX = allocWidth;
            currentY += rowHeight;
            rowHeight = allocHeight;
        }
    }

    // Check if we need to start a new row
    if (currentX + resolution > atlasSize) {
        currentX = 0;
        currentY += rowHeight;
        rowHeight = resolution;
    }

    // Check if allocation fits
    if (currentY + resolution > atlasSize) {
        violet::Log::warn("ShadowSystem", "Atlas full, cannot allocate {}x{}", resolution, resolution);
        return ShadowAtlasAllocation{{0, 0, 0, 0}, 0, 0, false};
    }

    ShadowAtlasAllocation alloc;
    alloc.rect = glm::vec4(
        static_cast<float>(currentX) / atlasSize,
        static_cast<float>(currentY) / atlasSize,
        static_cast<float>(resolution) / atlasSize,
        static_cast<float>(resolution) / atlasSize
    );
    alloc.resolution = resolution;
    alloc.lightIndex = lightIndex;
    alloc.inUse = true;

    allocations.push_back(alloc);

    return alloc;
}

void ShadowSystem::freeSpace(const ShadowAtlasAllocation& alloc) {
    for (auto& existing : allocations) {
        if (existing.lightIndex == alloc.lightIndex && existing.inUse) {
            existing.inUse = false;
            break;
        }
    }
}

void ShadowSystem::clearAllAllocations() {
    allocations.clear();
}

void ShadowSystem::ensureBufferCapacity(uint32_t shadowCount) {
    if (shadowCount <= bufferCapacity) return;

    uint32_t newCapacity = eastl::max(shadowCount, bufferCapacity * 2);
    newCapacity = eastl::min(newCapacity, MAX_SHADOWS);

    auto sets = descriptorManager->allocateSets("Shadow", maxFramesInFlight);

    for (uint32_t i = 0; i < maxFramesInFlight; i++) {
        ResourceFactory::destroyBuffer(context, shadowBuffers[i]);

        BufferInfo bufferInfo{
            .size = newCapacity * sizeof(ShadowData),
            .usage = vk::BufferUsageFlagBits::eStorageBuffer,
            .memoryUsage = MemoryUsage::CPU_TO_GPU,
            .debugName = "ShadowDataBuffer"
        };
        shadowBuffers[i] = ResourceFactory::createBuffer(context, bufferInfo);

        eastl::vector<ResourceBindingDesc> bindings;
        bindings.push_back(ResourceBindingDesc::storageBuffer(
            0, shadowBuffers[i].buffer, 0, newCapacity * sizeof(ShadowData)));
        descriptorManager->updateSet(sets[i], bindings);
    }

    bufferCapacity = newCapacity;
}

void ShadowSystem::createAtlas() {
    // Create depth texture through TextureManager
    auto atlasTexture = eastl::make_unique<Texture>();
    atlasTexture->createDepthTexture(
        context,
        atlasSize,
        atlasSize,
        vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled
    );

    // Set shadow sampler for the texture
    vk::Sampler depthSampler = descriptorManager->getSampler(SamplerType::Shadow);
    atlasTexture->setSampler(depthSampler);

    // Register to bindless texture array manually
    // Use a mid-range index to avoid conflicts with regular textures
    atlasBindlessIndex = 512;

    vk::DescriptorImageInfo descImageInfo;
    descImageInfo.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
    descImageInfo.imageView = atlasTexture->getImageView();
    descImageInfo.sampler = depthSampler;

    vk::WriteDescriptorSet write;
    write.dstSet = descriptorManager->getBindlessSet();
    write.dstBinding = 0;
    write.dstArrayElement = atlasBindlessIndex;
    write.descriptorCount = 1;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.pImageInfo = &descImageInfo;

    context->getDevice().updateDescriptorSets(1, &write, 0, nullptr);

    // Add texture to TextureManager and store handle
    atlasTextureHandle = textureManager->addTexture(eastl::move(atlasTexture));

    violet::Log::info("ShadowSystem", "Created shadow atlas {}x{} (bindless index: {}, handle: {})",
                     atlasSize, atlasSize, atlasBindlessIndex, atlasTextureHandle.index);
}

const ImageResource* ShadowSystem::getAtlasImage() const {
    if (!atlasTextureHandle.isValid() || !textureManager) {
        return nullptr;
    }

    const Texture* texture = textureManager->getTexture(atlasTextureHandle);
    if (!texture) {
        return nullptr;
    }

    return texture->getImageResource();
}

} // namespace violet