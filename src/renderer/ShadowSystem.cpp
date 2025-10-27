#include "ShadowSystem.hpp"
#include "LightingSystem.hpp"
#include "ecs/Components.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/DescriptorManager.hpp"
#include "renderer/camera/Camera.hpp"
#include "renderer/camera/PerspectiveCamera.hpp"
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

    // Calculate cascade split depths using Practical Split Scheme
    // Combines logarithmic and uniform splits based on lambda parameter
    eastl::vector<float> calculateCascadeSplits(float nearPlane, float farPlane,
                                                 uint32_t cascadeCount, float lambda) {
        eastl::vector<float> splits(cascadeCount + 1);
        splits[0] = nearPlane;
        splits[cascadeCount] = farPlane;

        for (uint32_t i = 1; i < cascadeCount; i++) {
            float p = static_cast<float>(i) / static_cast<float>(cascadeCount);

            // Logarithmic split (denser near camera)
            float logSplit = nearPlane * std::pow(farPlane / nearPlane, p);

            // Uniform split
            float uniformSplit = nearPlane + (farPlane - nearPlane) * p;

            // Practical split: lerp between logarithmic and uniform
            splits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
        }

        return splits;
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

    descriptorSets = descriptorManager->allocateSets("Shadow", maxFramesInFlight);

    for (uint32_t i = 0; i < maxFramesInFlight; i++) {
        eastl::vector<ResourceBindingDesc> bindings;
        bindings.push_back(ResourceBindingDesc::storageBuffer(
            0, shadowBuffers[i].buffer, 0, bufferCapacity * sizeof(ShadowData)));
        descriptorManager->updateSet(descriptorSets[i], bindings);
    }

    createAtlas();

    violet::Log::info("ShadowSystem", "Initialized (atlas: {}x{}, capacity: {})",
                      atlasSize, atlasSize, INITIAL_CAPACITY);
}

void ShadowSystem::cleanup() {
    if (!context) return;

    descriptorSets.clear();

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

void ShadowSystem::update(entt::registry& world, LightingSystem& lightingSystem, Camera* camera, uint32_t frameIndex, const AABB& sceneBounds) {
    cpuShadowData.clear();
    clearAllAllocations();
    shadowRenderables.clear();

    if (!camera) {
        violet::Log::warn("ShadowSystem", "No active camera, skipping shadow update");
        return;
    }

    // Collect ALL potentially shadow-casting objects from the world
    // (Not camera-frustum culled - we need objects outside camera view that can still cast shadows into it)
    auto entityView = world.view<TransformComponent, MeshComponent>();
    for (auto entity : entityView) {
        auto& transform = entityView.get<TransformComponent>(entity);
        auto& meshComp = entityView.get<MeshComponent>(entity);

        if (!meshComp.mesh) continue;

        Mesh* mesh = meshComp.mesh.get();
        glm::mat4 worldTransform = transform.world.getMatrix();

        // Update world bounds if needed
        if (meshComp.dirty || transform.dirty) {
            meshComp.updateWorldBounds(worldTransform);
        }

        const auto& subMeshes = mesh->getSubMeshes();
        for (size_t i = 0; i < subMeshes.size(); ++i) {
            const SubMesh& subMesh = subMeshes[i];
            if (!subMesh.isValid()) continue;

            // Get material for this submesh
            Material* material = nullptr;
            if (auto* matComp = world.try_get<MaterialComponent>(entity)) {
                // TODO: Get actual material from MaterialComponent
                // For now, we'll add all objects as potential shadow casters
            }

            Renderable renderable(
                entity,
                mesh,
                material,
                worldTransform,
                static_cast<uint32_t>(i)
            );
            renderable.visible = true;
            shadowRenderables.push_back(renderable);
        }
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

        // Build shadow data
        ShadowData shadowData{};
        shadowData.shadowParams = glm::vec4(light.shadowBias, light.shadowNormalBias, 0.05f, 0.0f); // z=blendRange
        shadowData.lightType = (light.type == LightType::Directional) ? 0 : 1;
        shadowData.atlasIndex = atlasBindlessIndex;

        // Compute light space matrices
        if (light.type == LightType::Directional) {
            glm::vec3 lightDir = glm::normalize(light.direction);
            glm::vec3 up = (std::abs(lightDir.y) < 0.999f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);

            // Try to use CSM if camera is PerspectiveCamera
            PerspectiveCamera* perspCam = dynamic_cast<PerspectiveCamera*>(camera);
            uint32_t cascadeCount = (perspCam && light.cascadeCount > 1) ? light.cascadeCount : 1;
            cascadeCount = eastl::min(cascadeCount, 4u);  // Max 4 cascades
            shadowData.cascadeCount = cascadeCount;

            // Calculate cascade split depths
            float nearPlane = perspCam ? perspCam->getNearPlane() : 0.1f;
            float farPlane = perspCam ? perspCam->getFarPlane() : 100.0f;
            auto splits = calculateCascadeSplits(nearPlane, farPlane, cascadeCount, light.cascadeSplitLambda);

            // For each cascade, compute light space matrix and allocate atlas space
            for (uint32_t c = 0; c < cascadeCount; c++) {
                // Create projection matrix for this cascade's frustum slice
                float cascadeNear = splits[c];
                float cascadeFar = splits[c + 1];

                // Build projection matrix for cascade sub-frustum
                glm::mat4 cascadeProj = proj;  // Start with camera's projection
                if (perspCam) {
                    // Override near/far planes for this cascade
                    cascadeProj = glm::perspective(
                        glm::radians(perspCam->getFOV()),
                        perspCam->getAspectRatio(),
                        cascadeNear,
                        cascadeFar
                    );
                }

                // Get frustum corners for this cascade
                auto frustumCorners = getFrustumCornersWorldSpace(cascadeProj, view);

                // Compute frustum center
                glm::vec3 center = glm::vec3(0.0f);
                for (const auto& corner : frustumCorners) {
                    center += corner;
                }
                center /= frustumCorners.size();

                // Build light view matrix looking at cascade frustum center
                glm::mat4 lightViewMatrix = glm::lookAt(
                    center - lightDir * light.shadowFarPlane,  // Position light behind frustum
                    center,                                     // Look at frustum center
                    up
                );

                // Transform frustum corners to light space and find bounds
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

                // ============= Optimization 1: Extend bounds using Scene AABB =============
                // Use scene bounds to extend XY coverage and Z range
                if (sceneBounds.isValid()) {
                    // Transform scene AABB corners to light space
                    glm::vec3 sceneMin = sceneBounds.min;
                    glm::vec3 sceneMax = sceneBounds.max;

                    for (int x = 0; x <= 1; x++) {
                        for (int y = 0; y <= 1; y++) {
                            for (int z = 0; z <= 1; z++) {
                                glm::vec3 corner(
                                    x ? sceneMax.x : sceneMin.x,
                                    y ? sceneMax.y : sceneMin.y,
                                    z ? sceneMax.z : sceneMin.z
                                );
                                glm::vec4 lightSpaceCorner = lightViewMatrix * glm::vec4(corner, 1.0f);

                                // Expand XY bounds to include scene objects
                                minX = std::min(minX, lightSpaceCorner.x);
                                maxX = std::max(maxX, lightSpaceCorner.x);
                                minY = std::min(minY, lightSpaceCorner.y);
                                maxY = std::max(maxY, lightSpaceCorner.y);

                                // Expand Z range to include scene objects
                                minZ = std::min(minZ, lightSpaceCorner.z);
                                maxZ = std::max(maxZ, lightSpaceCorner.z);
                            }
                        }
                    }

                    // Add extra padding to Z range to ensure all shadow casters are included
                    // Shadow casters outside the frustum can still cast shadows into it
                    float zRange = maxZ - minZ;
                    float zPadding = zRange * 1.0f;  // 100% padding on both sides
                    minZ = minZ - zPadding;  // Extend backwards (away from light)
                    maxZ = maxZ + zPadding;  // Extend forwards (towards light)
                } else {
                    // Fallback: extend bounds to cover area beyond frustum
                    float shadowExtent = 3.0f;  // 3x coverage (increased from 2.0x)
                    float centerX = (minX + maxX) * 0.5f;
                    float centerY = (minY + maxY) * 0.5f;
                    float extentX = (maxX - minX) * shadowExtent * 0.5f;
                    float extentY = (maxY - minY) * shadowExtent * 0.5f;

                    minX = centerX - extentX;
                    maxX = centerX + extentX;
                    minY = centerY - extentY;
                    maxY = centerY + extentY;

                    // Extend Z range in both directions to avoid clipping shadow casters
                    float zRange = maxZ - minZ;
                    minZ = minZ - zRange * 5.0f;  // Extend backwards
                    maxZ = maxZ + zRange * 2.0f;  // Extend forwards
                }

                // ============= Optimization 2: Projection Quantization for Stability =============
                // Quantize projection bounds to fixed increments to reduce jitter
                float quantize = 64.0f;  // World-space quantization step (adjustable)

                minX = std::floor(minX / quantize) * quantize;
                maxX = std::ceil(maxX / quantize) * quantize;
                minY = std::floor(minY / quantize) * quantize;
                maxY = std::ceil(maxY / quantize) * quantize;

                // Calculate projection dimensions
                float projWidth = maxX - minX;
                float projHeight = maxY - minY;

                // ============= Optimization 3: Texel Snapping to Prevent Shimmer =============
                // Get cascade resolution for texel size calculation
                uint32_t cascadeResolution = light.shadowResolution >> c;
                cascadeResolution = eastl::max(cascadeResolution, 256u);

                // Calculate texel size in light space
                float texelSizeX = projWidth / cascadeResolution;
                float texelSizeY = projHeight / cascadeResolution;

                // Snap min bounds to texel grid
                minX = std::floor(minX / texelSizeX) * texelSizeX;
                minY = std::floor(minY / texelSizeY) * texelSizeY;

                // Recalculate max bounds to maintain consistent size
                maxX = minX + projWidth;
                maxY = minY + projHeight;

                // Build stabilized orthographic projection
                glm::mat4 lightProjMatrix = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);
                shadowData.cascadeViewProjMatrices[c] = lightProjMatrix * lightViewMatrix;

                // Allocate atlas space for this cascade
                auto cascadeAlloc = allocateSpace(cascadeResolution, lightIndex * 10 + c);

                if (!cascadeAlloc.inUse) {
                    // Atlas full, skip remaining cascades
                    shadowData.cascadeCount = c;
                    break;
                }

                shadowData.atlasRects[c] = cascadeAlloc.rect;

                // Store cascade split depth (view space Z, which is negative in right-handed view space)
                // We store the far plane of each cascade
                if (c < 3) {  // Only store first 3 splits (4th is implicit as camera far plane)
                    shadowData.cascadeSplitDepths[c] = cascadeFar;
                }
            }

            // ============= Static Fallback Cascade (Entire Scene AABB) =============
            // Add a fallback cascade covering the entire scene as insurance
            if (sceneBounds.isValid() && cascadeCount < 4) {
                uint32_t fallbackIdx = cascadeCount;

                // Center of scene AABB
                glm::vec3 sceneCenter = (sceneBounds.min + sceneBounds.max) * 0.5f;

                // Light view matrix pointing at scene center
                glm::mat4 lightView = glm::lookAt(
                    sceneCenter - lightDir * 500.0f,  // Far enough back
                    sceneCenter,
                    up
                );

                // Project scene AABB corners to light space
                float minX = FLT_MAX, maxX = -FLT_MAX;
                float minY = FLT_MAX, maxY = -FLT_MAX;
                float minZ = FLT_MAX, maxZ = -FLT_MAX;

                for (int x = 0; x <= 1; x++) {
                    for (int y = 0; y <= 1; y++) {
                        for (int z = 0; z <= 1; z++) {
                            glm::vec3 corner(
                                x ? sceneBounds.max.x : sceneBounds.min.x,
                                y ? sceneBounds.max.y : sceneBounds.min.y,
                                z ? sceneBounds.max.z : sceneBounds.min.z
                            );
                            glm::vec4 lsCorner = lightView * glm::vec4(corner, 1.0f);
                            minX = std::min(minX, lsCorner.x);
                            maxX = std::max(maxX, lsCorner.x);
                            minY = std::min(minY, lsCorner.y);
                            maxY = std::max(maxY, lsCorner.y);
                            minZ = std::min(minZ, lsCorner.z);
                            maxZ = std::max(maxZ, lsCorner.z);
                        }
                    }
                }

                // Orthographic projection covering entire scene
                glm::mat4 lightProj = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);
                shadowData.cascadeViewProjMatrices[fallbackIdx] = lightProj * lightView;

                // Allocate atlas space
                auto fallbackAlloc = allocateSpace(1024, lightIndex * 10 + fallbackIdx);
                if (fallbackAlloc.inUse) {
                    shadowData.atlasRects[fallbackIdx] = fallbackAlloc.rect;
                    shadowData.cascadeSplitDepths[fallbackIdx] = FLT_MAX;  // Always use as last resort
                    shadowData.cascadeCount++;
                }
            }

            // If we only got 1 cascade or CSM failed, fallback completed above
        } else if (light.type == LightType::Point) {
            // Point light: allocate single cubemap space in atlas
            shadowData.cascadeCount = 1;

            uint32_t resolution = light.shadowResolution;
            auto alloc = allocateSpace(resolution, lightIndex);

            if (!alloc.inUse) {
                lightIndex++;
                continue; // Atlas full, skip this point light
            }

            shadowData.atlasRects[0] = alloc.rect;

            // Generate 6 cube face matrices
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
    if (frameIndex >= descriptorSets.size()) {
        return vk::DescriptorSet{};
    }
    return descriptorSets[frameIndex];
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

    descriptorSets = descriptorManager->allocateSets("Shadow", maxFramesInFlight);

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
        descriptorManager->updateSet(descriptorSets[i], bindings);
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