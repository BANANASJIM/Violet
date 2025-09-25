#include "Renderer.hpp"

#include <glm/glm.hpp>

#include <EASTL/unique_ptr.h>

#include "core/Log.hpp"
#include "ecs/Components.hpp"
#include "renderer/Camera.hpp"
#include "renderer/DescriptorSet.hpp"
#include "renderer/Material.hpp"
#include "renderer/Mesh.hpp"
#include "renderer/Pipeline.hpp"
#include "renderer/RenderPass.hpp"
#include "renderer/UniformBuffer.hpp"
#include "renderer/VulkanContext.hpp"

namespace violet {

Renderer::~Renderer() {
    cleanup();
}

void Renderer::init(VulkanContext* ctx, RenderPass* rp, uint32_t framesInFlight) {
    context           = ctx;
    renderPass        = rp;
    maxFramesInFlight = framesInFlight;
    globalUniforms.init(context, maxFramesInFlight);
    debugRenderer.init(context, renderPass, &globalUniforms, maxFramesInFlight);
}

void Renderer::cleanup() {
    debugRenderer.cleanup();
    globalUniforms.cleanup();
    materialInstances.clear();
    materials.clear();
    textures.clear();
    renderables.clear();
    renderableCache.clear();
}

void Renderer::collectRenderables(entt::registry& world) {
    renderables.clear();

    auto view = world.view<TransformComponent, MeshComponent>();


    for (auto entity : view) {
        collectFromEntity(entity, world);
    }
}

void Renderer::updateGlobalUniforms(entt::registry& world, uint32_t frameIndex) {
    globalUniforms.update(world, frameIndex);
}

void Renderer::collectFromEntity(entt::entity entity, entt::registry& world) {
    auto* transform = world.try_get<TransformComponent>(entity);
    auto* meshComp  = world.try_get<MeshComponent>(entity);

    if (!transform || !meshComp || !meshComp->mesh) {
        return;
    }

    Mesh*     mesh           = meshComp->mesh.get();
    glm::mat4 worldTransform = transform->world.getMatrix();


    // Update world bounds if dirty
    if (meshComp->dirty || transform->dirty) {
        meshComp->updateWorldBounds(worldTransform);
    }

    const auto& subMeshes = mesh->getSubMeshes();


    for (size_t i = 0; i < subMeshes.size(); ++i) {
        const SubMesh& subMesh = subMeshes[i];
        if (!subMesh.isValid()) {
            VT_WARN("Entity {} submesh {} is invalid (indexCount={})",
                static_cast<uint32_t>(entity), i, subMesh.indexCount);
            continue;
        }

        MaterialInstance* matInstance = nullptr;

        if (auto* matComp = world.try_get<MaterialComponent>(entity)) {
            // Get the global material ID from the SubMesh's material index
            uint32_t materialId = matComp->getMaterialId(subMesh.materialIndex);
            matInstance = getMaterialInstanceByIndex(materialId);
        } else {
        }

        Renderable renderable(
            entity,
            mesh,
            matInstance ? matInstance->getMaterial() : nullptr,
            worldTransform,
            static_cast<uint32_t>(i)
        );
        renderable.visible = true;
        renderable.dirty   = meshComp->dirty || transform->dirty;

        renderables.push_back(renderable);
    }

    meshComp->dirty  = false;
    transform->dirty = false;
}

void Renderer::setViewport(vk::CommandBuffer commandBuffer, const vk::Extent2D& extent) {
    vk::Viewport viewport;
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(extent.width);
    viewport.height   = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    commandBuffer.setViewport(0, 1, &viewport);

    vk::Rect2D scissor;
    scissor.offset = vk::Offset2D{0, 0};
    scissor.extent = extent;
    commandBuffer.setScissor(0, 1, &scissor);
}

void Renderer::buildSceneBVH(entt::registry& world) {
    // Build BVH from renderables
    renderableBounds.clear();
    renderableBounds.reserve(renderables.size());

    // Force update all world bounds before building BVH
    for (size_t i = 0; i < renderables.size(); i++) {
        const auto& renderable = renderables[i];
        if (renderable.mesh) {
            auto* meshComp = world.try_get<MeshComponent>(renderable.entity);
            if (meshComp) {
                // Force update world bounds with current transform
                meshComp->updateWorldBounds(renderable.worldTransform);

                // Use SubMesh-specific bounds instead of entire mesh bounds
                uint32_t subMeshIndex = renderable.subMeshIndex;
                if (subMeshIndex < meshComp->getSubMeshCount()) {
                    renderableBounds.push_back(meshComp->getSubMeshWorldBounds(subMeshIndex));

                    // Debug logging for first few renderables
                    if (i < 5) {
                        const auto& bounds = meshComp->getSubMeshWorldBounds(subMeshIndex);
                        VT_INFO("Renderable {} SubMesh {} AABB: min({:.2f}, {:.2f}, {:.2f}) max({:.2f}, {:.2f}, {:.2f})",
                                i, subMeshIndex, bounds.min.x, bounds.min.y, bounds.min.z,
                                bounds.max.x, bounds.max.y, bounds.max.z);
                    }
                } else {
                    VT_WARN("Invalid subMeshIndex {} for renderable {}", subMeshIndex, i);
                    // Fallback to legacy bounds
                    renderableBounds.push_back(meshComp->worldBounds);
                }
            } else {
                // Fallback: transform local bounds - this shouldn't happen anymore
                VT_WARN("No MeshComponent found for renderable {}", i);
                renderableBounds.push_back(renderable.mesh->getLocalBounds().transform(renderable.worldTransform));
            }
        }
    }

    // Build BVH once for the scene
    sceneBVH.build(renderableBounds);
    VT_INFO("Scene BVH built with {} renderables", renderables.size());

}

void Renderer::renderScene(vk::CommandBuffer commandBuffer, uint32_t frameIndex, entt::registry& world) {

    // Get camera frustum for culling
    Camera* activeCamera = globalUniforms.findActiveCamera(world);
    if (!activeCamera) {
        return;
    }

    // Perform frustum culling
    const Frustum& frustum = activeCamera->getFrustum();

    // Debug: Log camera and frustum info
    glm::vec3 camPos = activeCamera->getPosition();
    glm::vec3 camTarget = activeCamera->getTarget();

    // Get view-projection matrix for debugging
    glm::mat4 viewMatrix = activeCamera->getViewMatrix();
    glm::mat4 projMatrix = activeCamera->getProjectionMatrix();
    glm::mat4 viewProjMatrix = projMatrix * viewMatrix;

    // Log culling stats occasionally
    static int frameCount = 0;
    frameCount++;
    bool shouldLog = (frameCount % 300 == 0);  // Log every ~5 seconds at 60fps

    // Log camera info and frustum planes occasionally
    if (shouldLog) {
        VT_INFO("Camera: pos({:.1f}, {:.1f}, {:.1f}) target({:.1f}, {:.1f}, {:.1f})",
                camPos.x, camPos.y, camPos.z, camTarget.x, camTarget.y, camTarget.z);

        // Basic frustum culling summary (detailed debug removed)
    }

    visibleIndices.clear();

    // Debug: Temporarily disable culling to test if it's the cause
    static bool disableCulling = false;  // Re-enable culling
    if (disableCulling) {
        // Render all objects without culling
        for (uint32_t i = 0; i < renderables.size(); ++i) {
            visibleIndices.push_back(i);
        }
        VT_DEBUG("Culling DISABLED: rendering all {} objects", renderables.size());
    } else {
        VT_DEBUG("Frustum culling with camera at ({:.1f}, {:.1f}, {:.1f})",
                activeCamera->getPosition().x, activeCamera->getPosition().y, activeCamera->getPosition().z);

        // Test new BVH implementation every frame
        VT_INFO("Building BVH with {} renderables", renderables.size());
        sceneBVH.build(renderableBounds);

        // Use new template-based BVH traversal
        sceneBVH.traverse(
            [&frustum](const AABB& bounds) -> bool {
                return frustum.testAABB(bounds);
            },
            [&](uint32_t primitiveIndex) {
                visibleIndices.push_back(primitiveIndex);
            }
        );

        VT_INFO("BVH traversal found {} visible renderables", visibleIndices.size());

        /* Original code - now using BVH instead
        // Bypass BVH and test each renderable directly
        for (uint32_t i = 0; i < renderables.size(); ++i) {
            if (i >= renderableBounds.size()) {
                VT_WARN("Missing bounds for renderable {}", i);
                continue;
            }

            const AABB& bounds = renderableBounds[i];

            // Use detailed debug testing for first 3 objects
            bool visible = (i < 3) ?
                frustum.testAABBDebug(bounds, i) :
                frustum.testAABB(bounds);

            if (visible) {
                visibleIndices.push_back(i);
            }
        }
        */

        if (shouldLog) {
            float cullingRate = renderables.empty() ? 0.0f
                : (1.0f - float(visibleIndices.size()) / float(renderables.size())) * 100.0f;
            VT_INFO("Culling stats: {}/{} renderables visible ({:.1f}% culled)",
                    visibleIndices.size(), renderables.size(), cullingRate);

            // Debug: Show which objects are visible and their positions
            if (!visibleIndices.empty()) {
                VT_INFO("Visible object indices: [{}{}]",
                        visibleIndices.size() <= 10 ? "" : "showing first 10 of ",
                        [&]() {
                            eastl::string indices;
                            for (size_t i = 0; i < eastl::min(size_t(10), visibleIndices.size()); ++i) {
                                if (i > 0) indices += ", ";
                                indices += std::to_string(visibleIndices[i]).c_str();
                            }
                            return indices;
                        }());

                // Show positions of first few visible objects and re-test them with debug info
                for (size_t i = 0; i < eastl::min(size_t(3), visibleIndices.size()); ++i) {
                    uint32_t idx = visibleIndices[i];
                    if (idx < renderableBounds.size()) {
                        const auto& bounds = renderableBounds[idx];
                        glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
                        glm::vec3 size = bounds.max - bounds.min;

                        VT_INFO("  Visible obj {}: AABB center({:.1f}, {:.1f}, {:.1f}) size({:.1f}, {:.1f}, {:.1f})",
                                idx, center.x, center.y, center.z, size.x, size.y, size.z);

                        // Distance from camera
                        float distance = glm::length(center - camPos);
                        VT_INFO("    Distance from camera: {:.1f} units", distance);

                        // Re-test with debug info
                        VT_INFO("    Re-testing with detailed frustum check:");
                        bool passes = frustum.testAABBDebug(bounds, static_cast<int>(i));
                        VT_INFO("    Final result: {}", passes ? "SHOULD BE VISIBLE" : "SHOULD BE CULLED");
                    }
                }
            }

            // Debug: Check for duplicate indices in BVH traversal
            eastl::vector<uint32_t> sortedIndices = visibleIndices;
            std::sort(sortedIndices.begin(), sortedIndices.end());
            auto duplicateEnd = eastl::unique(sortedIndices.begin(), sortedIndices.end());
            uint32_t uniqueCount = static_cast<uint32_t>(duplicateEnd - sortedIndices.begin());

            if (uniqueCount != visibleIndices.size()) {
                VT_WARN("BVH returned {} visible indices but only {} unique - {} duplicates!",
                        visibleIndices.size(), uniqueCount, visibleIndices.size() - uniqueCount);

                // Show first few duplicates
                eastl::hash_map<uint32_t, uint32_t> indexCount;
                for (uint32_t idx : visibleIndices) {
                    indexCount[idx]++;
                }

                int duplicatesShown = 0;
                for (const auto& pair : indexCount) {
                    if (pair.second > 1 && duplicatesShown < 5) {
                        VT_WARN("  Index {} appears {} times", pair.first, pair.second);
                        duplicatesShown++;
                    }
                }
            }
        }
    }

    Material* currentMaterial = nullptr;
    Mesh*     currentMesh     = nullptr;
    uint32_t  drawCallCount   = 0;

    // Only render visible renderables
    uint32_t skippedCount = 0;
    uint32_t drawnCount = 0;

    for (uint32_t idx : visibleIndices) {
        if (idx >= renderables.size()) {
            skippedCount++;
            continue;
        }
        const auto& renderable = renderables[idx];
        if (!renderable.visible || !renderable.mesh) {
            skippedCount++;
            continue;
        }

        // Mesh binding
        if (renderable.mesh != currentMesh) {
            currentMesh = renderable.mesh;
            vk::Buffer vertexBuffer = currentMesh->getVertexBuffer().getBuffer();
            vk::Buffer indexBuffer = currentMesh->getIndexBuffer().getBuffer();

            if (vertexBuffer && indexBuffer) {
                commandBuffer.bindVertexBuffers(0, vertexBuffer, {0});
                commandBuffer.bindIndexBuffer(indexBuffer, 0, currentMesh->getIndexBuffer().getIndexType());
            } else {
                continue;
            }
        }

        // Material binding
        if (renderable.material != currentMaterial) {
            currentMaterial = renderable.material;
            if (currentMaterial && currentMaterial->getPipeline()) {
                currentMaterial->getPipeline()->bind(commandBuffer);

                // 绑定全局descriptor set (set 0)
                vk::DescriptorSet globalSet = globalUniforms.getDescriptorSet()->getDescriptorSet(frameIndex);
                commandBuffer.bindDescriptorSets(
                    vk::PipelineBindPoint::eGraphics,
                    currentMaterial->getPipelineLayout(),
                    GLOBAL_SET,
                    1,
                    &globalSet,
                    0,
                    nullptr
                );
            } else {
                continue;
            }
        }

        // 绑定MaterialInstance的descriptor set (set 1) - 使用SubMesh的materialIndex
        MaterialInstance* matInstance = nullptr;
        if (auto* matComp = world.try_get<MaterialComponent>(renderable.entity)) {
            const SubMesh& subMesh = currentMesh->getSubMesh(renderable.subMeshIndex);
            uint32_t materialId = matComp->getMaterialId(subMesh.materialIndex);
            matInstance = getMaterialInstanceByIndex(materialId);
        }

        if (matInstance && matInstance->getDescriptorSet()) {
            // Update material descriptor set for current frame
            matInstance->updateDescriptorSet(frameIndex);

            vk::DescriptorSet materialSet = matInstance->getDescriptorSet()->getDescriptorSet(frameIndex);
            commandBuffer.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                currentMaterial->getPipelineLayout(),
                MATERIAL_SET,
                1,
                &materialSet,
                0,
                nullptr
            );
        } else {
            violet::Log::warn(
                "Renderer",
                "Material instance or descriptor set is null: matInstance={}, descriptorSet={}",
                matInstance != nullptr,
                matInstance ? (matInstance->getDescriptorSet() != nullptr) : false
            );
        }


        // Push model matrix as push constant
        PushConstantData pushData;
        pushData.model = renderable.worldTransform;

        commandBuffer.pushConstants(
            currentMaterial->getPipelineLayout(),
            vk::ShaderStageFlagBits::eVertex,
            0,
            sizeof(PushConstantData),
            &pushData
        );

        // Draw call
        const SubMesh& subMesh = currentMesh->getSubMesh(renderable.subMeshIndex);
        commandBuffer.drawIndexed(subMesh.indexCount, 1, subMesh.firstIndex, 0, 0);
        drawCallCount++;
        drawnCount++;
    }

    // Log rendering statistics
    static int renderFrameCount = 0;
    renderFrameCount++;
    if (renderFrameCount % 300 == 0) {  // Log every ~5 seconds
        VT_INFO("Rendering stats: {}/{} visible objects processed, {} drawn, {} skipped, {} draw calls",
                drawnCount + skippedCount, visibleIndices.size(), drawnCount, skippedCount, drawCallCount);
    }

    // Debug rendering (after main scene rendering)
    if (debugRenderer.isEnabled()) {
        if (debugRenderer.showFrustum()) {
            debugRenderer.renderFrustum(commandBuffer, frameIndex, frustum);
        }

        if (debugRenderer.showAABBs()) {
            // Collect SubMesh AABBs and visibility info
            eastl::vector<AABB> aabbs;
            eastl::vector<bool> visibility;
            aabbs.reserve(renderables.size());
            visibility.reserve(renderables.size());

            for (size_t i = 0; i < renderables.size(); ++i) {
                const auto& renderable = renderables[i];
                if (renderable.mesh) {
                    auto* meshComp = world.try_get<MeshComponent>(renderable.entity);
                    if (meshComp) {
                        // Use SubMesh-specific AABB instead of entire mesh AABB
                        uint32_t subMeshIndex = renderable.subMeshIndex;
                        if (subMeshIndex < meshComp->getSubMeshCount()) {
                            aabbs.push_back(meshComp->getSubMeshWorldBounds(subMeshIndex));
                        } else {
                            // Fallback to legacy bounds if something is wrong
                            aabbs.push_back(meshComp->worldBounds);
                        }

                        // Check if this renderable index is in visibleIndices
                        bool isVisible = eastl::find(visibleIndices.begin(), visibleIndices.end(), i) != visibleIndices.end();
                        visibility.push_back(isVisible);
                    }
                }
            }

            debugRenderer.renderAABBs(commandBuffer, frameIndex, aabbs, visibility);
        }
    }
}

Material* Renderer::createMaterial(const eastl::string& vertexShader, const eastl::string& fragmentShader) {
    // Default to PBR material
    return createMaterial(vertexShader, fragmentShader, DescriptorSetType::MaterialTextures);
}

Material* Renderer::createMaterial(const eastl::string& vertexShader, const eastl::string& fragmentShader, DescriptorSetType materialType) {
    // Creating material with specified shaders and type

    auto material = eastl::make_unique<Material>();

    try {
        // Material creates its descriptor set layout based on type
        material->create(context, materialType);
        // Material descriptor set layout created
    } catch (const std::exception& e) {
        violet::Log::error("Renderer", "Failed to create material with descriptor set type {}: {}", static_cast<int>(materialType), e.what());
        return nullptr;
    }

    // 创建pipeline，使用global和material的descriptor set layout
    auto pipeline = new Pipeline();
    try {
        pipeline->init(context, renderPass, globalUniforms.getDescriptorSet(), material.get(), vertexShader, fragmentShader);
        // Pipeline initialized
    } catch (const std::exception& e) {
        violet::Log::error("Renderer", "Failed to initialize pipeline with shaders: '{}', '{}' - Error: {}",
            vertexShader.c_str(), fragmentShader.c_str(), e.what());
        delete pipeline;
        return nullptr;
    }

    // Validate that the pipeline was created properly
    if (!pipeline->getPipeline()) {
        violet::Log::error("Renderer", "Pipeline creation failed - null pipeline object");
        delete pipeline;
        return nullptr;
    }

    material->pipeline = pipeline;

    Material* ptr = material.get();
    materials.push_back(eastl::move(material));

    // Material created successfully
    return ptr;
}

MaterialInstance* Renderer::createMaterialInstance(Material* material) {
    // 默认创建PBR材质实例
    return createPBRMaterialInstance(material);
}

MaterialInstance* Renderer::createPBRMaterialInstance(Material* material) {
    auto instance = eastl::make_unique<PBRMaterialInstance>();
    instance->create(context, material);

    // MaterialInstance创建自己的descriptor set
    instance->createDescriptorSet(maxFramesInFlight);

    MaterialInstance* ptr = instance.get();
    materialInstances.push_back(eastl::move(instance));

    return ptr;
}

MaterialInstance* Renderer::createUnlitMaterialInstance(Material* material) {
    if (!material) {
        violet::Log::error("Renderer", "Cannot create material instance - null material provided");
        return nullptr;
    }

    // Creating unlit material instance

    auto instance = eastl::make_unique<UnlitMaterialInstance>();

    try {
        instance->create(context, material);
        // Unlit material instance created
    } catch (const std::exception& e) {
        violet::Log::error("Renderer", "Failed to create unlit material instance: {}", e.what());
        return nullptr;
    }

    try {
        // MaterialInstance创建自己的descriptor set
        instance->createDescriptorSet(maxFramesInFlight);
        // Material instance descriptor set created
    } catch (const std::exception& e) {
        violet::Log::error("Renderer", "Failed to create descriptor set for material instance: {}", e.what());
        return nullptr;
    }

    MaterialInstance* ptr = instance.get();
    materialInstances.push_back(eastl::move(instance));

    // Unlit material instance creation completed
    return ptr;
}

void Renderer::registerMaterialInstance(uint32_t index, MaterialInstance* instance) {
    globalMaterialIndex[index] = instance;
}

MaterialInstance* Renderer::getMaterialInstanceByIndex(uint32_t index) const {
    auto it = globalMaterialIndex.find(index);
    return it != globalMaterialIndex.end() ? it->second : nullptr;
}

Texture* Renderer::addTexture(eastl::unique_ptr<Texture> texture) {
    Texture* ptr = texture.get();
    textures.push_back(eastl::move(texture));
    return ptr;
}

// GlobalUniforms implementation
GlobalUniforms::~GlobalUniforms() {
    cleanup();
}

void GlobalUniforms::init(VulkanContext* ctx, uint32_t maxFramesInFlight) {
    context = ctx;

    // Create global descriptor set using new unified approach
    descriptorSet = eastl::make_unique<DescriptorSet>();
    descriptorSet->create(context, maxFramesInFlight, DescriptorSetType::GlobalUniforms);

    uniformBuffers.resize(maxFramesInFlight);
    for (uint32_t i = 0; i < maxFramesInFlight; ++i) {
        uniformBuffers[i] = eastl::make_unique<UniformBuffer>();
        uniformBuffers[i]->create(context, sizeof(GlobalUBO));

        // Update descriptor set with uniform buffer
        descriptorSet->updateBuffer(i, uniformBuffers[i].get());
    }
}

void GlobalUniforms::cleanup() {
    // GlobalUniforms cleanup
    // descriptorSet的析构函数会自动调用cleanup，不需要手动调用
    uniformBuffers.clear();
    descriptorSet.reset();
}

Camera* GlobalUniforms::findActiveCamera(entt::registry& world) {
    auto view = world.view<CameraComponent>();
    for (auto entity : view) {
        auto& cameraComp = view.get<CameraComponent>(entity);
        if (cameraComp.isActive && cameraComp.camera) {
            return cameraComp.camera.get();
        }
    }
    return nullptr;
}

void GlobalUniforms::update(entt::registry& world, uint32_t frameIndex) {
    Camera* activeCamera = findActiveCamera(world);
    if (!activeCamera) {
        VT_WARN("No active camera found!");
        return;
    }

    cachedUBO.view      = activeCamera->getViewMatrix();
    cachedUBO.proj      = activeCamera->getProjectionMatrix();
    cachedUBO.cameraPos = activeCamera->getPosition();


    uniformBuffers[frameIndex]->update(&cachedUBO, sizeof(cachedUBO));
    // REMOVED: descriptorSet->updateBuffer() - This was causing the UBO data to be lost!
    // The descriptor set is already bound to the buffer during initialization,
    // we only need to update the buffer contents, not rebind the descriptor set.
}

} // namespace violet
