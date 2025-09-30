#include "ForwardRenderer.hpp"

#include <glm/glm.hpp>

#include <EASTL/unique_ptr.h>

#include "renderer/ResourceFactory.hpp"
#include "renderer/Buffer.hpp"

#include "core/Log.hpp"
#include "ui/SceneDebugLayer.hpp"
#include "ecs/Components.hpp"
#include "renderer/Camera.hpp"
#include "renderer/DescriptorSet.hpp"
#include "renderer/Material.hpp"
#include "renderer/Mesh.hpp"
#include "renderer/GraphicsPipeline.hpp"
#include "renderer/RenderPass.hpp"
#include "renderer/UniformBuffer.hpp"
#include "renderer/VulkanContext.hpp"

namespace violet {

ForwardRenderer::~ForwardRenderer() {
    cleanup();
}

void ForwardRenderer::init(VulkanContext* ctx, vk::Format swapchainFormat, uint32_t framesInFlight) {
    context = ctx;
    maxFramesInFlight = framesInFlight;

    // Setup multi-pass system
    setupPasses(swapchainFormat);

    // Initialize subsystems - use first pass for components that need RenderPass
    globalUniforms.init(context, maxFramesInFlight);
    if (!renderPasses.empty()) {
        // All rendering now happens in the unified Main pass (index 0)
        debugRenderer.init(context, &renderPasses[0], &globalUniforms, maxFramesInFlight);
        environmentMap.init(context, &renderPasses[0], this);
    }

    // Load HDR environment map
    environmentMap.loadHDR("assets/textures/skybox.hdr");

    // Set the environment texture in the global uniforms
    if (environmentMap.getEnvironmentTexture()) {
        globalUniforms.setSkyboxTexture(environmentMap.getEnvironmentTexture());
        environmentMap.setEnabled(true);
    }

    createDefaultPBRTextures();
}

void ForwardRenderer::cleanup() {
    for (auto& pass : renderPasses) {
        pass.cleanup();
    }
    renderPasses.clear();
    debugRenderer.cleanup();
    globalUniforms.cleanup();
    environmentMap.cleanup();
    materialInstances.clear();
    materials.clear();
    textures.clear();
    renderables.clear();
    renderableCache.clear();
}


void ForwardRenderer::beginFrame(entt::registry& world, uint32_t frameIndex) {
    currentWorld = &world;
    updateGlobalUniforms(world, frameIndex);
    collectRenderables(world);
}

void ForwardRenderer::renderFrame(vk::CommandBuffer cmd, vk::Framebuffer framebuffer, vk::Extent2D extent, uint32_t frameIndex) {
    currentExtent = extent;

    for (size_t i = 0; i < renderPasses.size(); ++i) {
        // Insert explicit barrier between passes if needed
        if (i > 0) {
            insertPassTransition(cmd, i);
        }

        // Execute pass
        auto& pass = renderPasses[i];

        // Swapchain passes use external framebuffer
        if (pass.getConfig().isSwapchainPass) {
            // Set external framebuffer for swapchain pass
            pass.setExternalFramebuffer(framebuffer);
            pass.begin(cmd, extent); // Will use external framebuffer
        } else {
            // Passes with own framebuffers use them directly
            pass.begin(cmd, extent); // Will use own framebuffer
        }

        pass.execute(cmd, frameIndex);
        pass.end(cmd);
    }
}

void ForwardRenderer::endFrame() {
    currentWorld = nullptr;
}

vk::RenderPass ForwardRenderer::getFinalPassRenderPass() const {
    if (renderPasses.empty()) {
        violet::Log::error("Renderer", "No render passes available");
        return VK_NULL_HANDLE;
    }
    return renderPasses.back().getRenderPass();
}

void ForwardRenderer::setupPasses(vk::Format swapchainFormat) {
    passConfigs.clear();
    renderPasses.clear();

    // Clear values
    vk::ClearValue colorClear;
    colorClear.color = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
    vk::ClearValue depthClear;
    depthClear.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

    // Main pass - single unified pass for all rendering with depth testing
    // This approach reuses the current pipeline architecture while maintaining multi-stage execution
    passConfigs.push_back({
        .name = "Main",
        .colorAttachments = {AttachmentDesc::swapchainColor(swapchainFormat, vk::AttachmentLoadOp::eClear)},
        .depthAttachment = AttachmentDesc::depth(context->findDepthFormat(), vk::AttachmentLoadOp::eClear),
        .hasDepth = true,
        .clearValues = {colorClear, depthClear},
        .isSwapchainPass = true,  // Uses swapchain framebuffer for final output
        .createOwnFramebuffer = false,  // Uses external swapchain framebuffer with depth
        .srcStage = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
        .dstStage = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
        .srcAccess = {},
        .dstAccess = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
        .execute = [this](vk::CommandBuffer cmd, uint32_t frame) {
            if (currentWorld) {
                setViewport(cmd, currentExtent);

                // First render skybox (depth testing disabled, renders to background)
                Material* envMaterial = environmentMap.getMaterial();
                if (envMaterial) {
                    environmentMap.renderSkybox(cmd, frame, envMaterial->getPipelineLayout(),
                                              globalUniforms.getDescriptorSet()->getDescriptorSet(frame));
                }

                // Then render scene geometry (depth testing enabled)
                renderScene(cmd, frame, *currentWorld);
            }
        }
    });

    // Create actual RenderPass objects from configurations
    for (size_t i = 0; i < passConfigs.size(); ++i) {
        const auto& config = passConfigs[i];
        RenderPass pass;
        pass.init(context, config);

        // Create framebuffers for passes that need them
        if (config.createOwnFramebuffer) {
            pass.createFramebuffers(currentExtent);
        }

        renderPasses.push_back(eastl::move(pass));
    }
}

void ForwardRenderer::collectRenderables(entt::registry& world) {
    renderables.clear();
    // Don't reset sceneDirty here - it should only be reset after BVH rebuild

    auto view = world.view<TransformComponent, MeshComponent>();

    for (auto entity : view) {
        collectFromEntity(entity, world);
    }
}

void ForwardRenderer::updateGlobalUniforms(entt::registry& world, uint32_t frameIndex) {
    globalUniforms.update(world, frameIndex, environmentMap.getExposure(), environmentMap.getRotation(), environmentMap.isEnabled());
}

void ForwardRenderer::collectFromEntity(entt::entity entity, entt::registry& world) {
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
        sceneDirty = true; // Mark scene as dirty when any object is dirty
    }

    const auto& subMeshes = mesh->getSubMeshes();


    for (size_t i = 0; i < subMeshes.size(); ++i) {
        const SubMesh& subMesh = subMeshes[i];
        if (!subMesh.isValid()) {
            violet::Log::warn("Renderer", "Entity {} submesh {} is invalid (indexCount={})",
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

void ForwardRenderer::buildSceneBVH(entt::registry& world) {
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

                } else {
                    violet::Log::warn("Renderer", "Invalid subMeshIndex {} for renderable {}", subMeshIndex, i);
                    // Fallback to legacy bounds
                    renderableBounds.push_back(meshComp->worldBounds);
                }
            } else {
                // Fallback: transform local bounds - this shouldn't happen anymore
                violet::Log::warn("Renderer", "No MeshComponent found for renderable {}", i);
                renderableBounds.push_back(renderable.mesh->getLocalBounds().transform(renderable.worldTransform));
            }
        }
    }

    // Build BVH once for the scene
    sceneBVH.build(renderableBounds);
    violet::Log::info("Renderer", "Scene BVH built with {} renderables", renderables.size());

}

void ForwardRenderer::renderScene(vk::CommandBuffer commandBuffer, uint32_t frameIndex, entt::registry& world) {

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


    visibleIndices.clear();

    // Debug: Temporarily disable culling to test if it's the cause
    static bool disableCulling = false;  // Re-enable culling
    if (disableCulling) {
        // Render all objects without culling
        for (uint32_t i = 0; i < renderables.size(); ++i) {
            visibleIndices.push_back(i);
        }
        // Render all objects without culling (debug mode)
    } else {
        // Only rebuild BVH when objects have moved or changed
        if (!bvhBuilt || sceneDirty) {
            // Rebuild bounds when scene is dirty
            if (sceneDirty) {
                buildSceneBVH(world);
                violet::Log::info("Renderer", "Scene was dirty - rebuilt BVH with {} renderables", renderables.size());
            } else {
                sceneBVH.build(renderableBounds);
            }
            sceneDirty = false;
            bvhBuilt = true;
        }

        // Use BVH traversal for frustum culling
        sceneBVH.traverse(
            [&frustum](const AABB& bounds) -> bool {
                return frustum.testAABB(bounds);
            },
            [&](uint32_t primitiveIndex) {
                visibleIndices.push_back(primitiveIndex);
            }
        );
    }

    // Reset render statistics
    renderStats.totalRenderables = static_cast<uint32_t>(renderables.size());
    renderStats.visibleRenderables = static_cast<uint32_t>(visibleIndices.size());
    renderStats.drawCalls = 0;
    renderStats.skippedRenderables = 0;

    Material* currentMaterial = nullptr;
    Mesh*     currentMesh     = nullptr;

    // Only render visible renderables
    for (uint32_t idx : visibleIndices) {
        if (idx >= renderables.size()) {
            renderStats.skippedRenderables++;
            continue;
        }
        const auto& renderable = renderables[idx];
        if (!renderable.visible || !renderable.mesh) {
            renderStats.skippedRenderables++;
            continue;
        }

        // Use BaseRenderer's bindVertexIndexBuffers helper
        if (renderable.mesh != currentMesh) {
            currentMesh = renderable.mesh;
            bindVertexIndexBuffers(commandBuffer, currentMesh);
        }

        // Material binding
        if (renderable.material != currentMaterial) {
            currentMaterial = renderable.material;
            if (currentMaterial && currentMaterial->getPipeline()) {
                currentMaterial->getPipeline()->bind(commandBuffer);

                // Use BaseRenderer's bindGlobalDescriptors helper
                vk::DescriptorSet globalSet = globalUniforms.getDescriptorSet()->getDescriptorSet(frameIndex);
                bindGlobalDescriptors(commandBuffer, currentMaterial->getPipelineLayout(), globalSet, GLOBAL_SET);
            } else {
                continue;
            }
        }

        // Bind MaterialInstance's descriptor set (set 1) - 使用SubMesh的materialIndex
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
            // Use BaseRenderer's bindMaterialDescriptors helper
            bindMaterialDescriptors(commandBuffer, currentMaterial->getPipelineLayout(), materialSet, MATERIAL_SET);
        }

        // Use BaseRenderer's pushModelMatrix helper
        pushModelMatrix(commandBuffer, currentMaterial->getPipelineLayout(), renderable.worldTransform);

        // Draw call
        const SubMesh& subMesh = currentMesh->getSubMesh(renderable.subMeshIndex);
        commandBuffer.drawIndexed(subMesh.indexCount, 1, subMesh.firstIndex, 0, 0);
        renderStats.drawCalls++;
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

        // Render ray visualization using batched rendering
        extern SceneDebugLayer* g_currentSceneDebugLayer;
        if (g_currentSceneDebugLayer) {
            const auto& storedRays = g_currentSceneDebugLayer->getStoredRays();
            if (!storedRays.empty()) {
                // Begin batching all rays
                debugRenderer.beginRayBatch();

                // Add all valid rays to the batch
                for (const auto& ray : storedRays) {
                    if (std::isfinite(ray.origin.x) && std::isfinite(ray.origin.y) && std::isfinite(ray.origin.z) &&
                        std::isfinite(ray.direction.x) && std::isfinite(ray.direction.y) && std::isfinite(ray.direction.z) &&
                        std::isfinite(ray.length) && ray.length > 0.0f) {
                        debugRenderer.addRayToBatch(ray.origin, ray.direction, ray.length);
                    }
                }

                // Render all rays in one batch
                debugRenderer.renderRayBatch(commandBuffer, frameIndex);
            }
        }
        // Render selected entity wireframe outline
        debugRenderer.renderSelectedEntity(commandBuffer, frameIndex, world, *this);
    }
}

Material* ForwardRenderer::createMaterial(const eastl::string& vertexShader, const eastl::string& fragmentShader) {
    // Default to PBR material
    return createMaterial(vertexShader, fragmentShader, DescriptorSetType::MaterialTextures);
}

Material* ForwardRenderer::createMaterial(const eastl::string& vertexShader, const eastl::string& fragmentShader, DescriptorSetType materialType) {
    // Creating material with specified shaders and type

    auto material = eastl::make_unique<Material>();

    try {
        // Material creates its descriptor set layout based on type
        material->create(context, materialType);
        // Material descriptor set layout created
    } catch (const std::exception& e) {
        violet::Log::error("ForwardRenderer", "Failed to create material with descriptor set type {}: {}", static_cast<int>(materialType), e.what());
        return nullptr;
    }

    // Create pipeline using the final pass RenderPass
    auto pipeline = new GraphicsPipeline();
    try {
        pipeline->init(context, &renderPasses[0], globalUniforms.getDescriptorSet(), material.get(), vertexShader, fragmentShader);
        // Pipeline initialized
    } catch (const std::exception& e) {
        violet::Log::error("ForwardRenderer", "Failed to initialize pipeline with shaders: '{}', '{}' - Error: {}",
            vertexShader.c_str(), fragmentShader.c_str(), e.what());
        delete pipeline;
        return nullptr;
    }

    // Validate that the pipeline was created properly
    if (!pipeline->getPipeline()) {
        violet::Log::error("ForwardRenderer", "Pipeline creation failed - null pipeline object");
        delete pipeline;
        return nullptr;
    }

    material->pipeline = pipeline;

    Material* ptr = material.get();
    materials.push_back(eastl::move(material));

    // Material created successfully
    return ptr;
}

Material* ForwardRenderer::createMaterial(const eastl::string& vertexShader, const eastl::string& fragmentShader, DescriptorSetType materialType, const PipelineConfig& config) {
    // Creating material with specified shaders, type, and pipeline config

    auto material = eastl::make_unique<Material>();

    try {
        // Material creates its descriptor set layout based on type
        material->create(context, materialType);
        // Material descriptor set layout created
    } catch (const std::exception& e) {
        violet::Log::error("ForwardRenderer", "Failed to create material with descriptor set type {}: {}", static_cast<int>(materialType), e.what());
        return nullptr;
    }

    // Create pipeline with custom config using the final pass RenderPass
    auto pipeline = new GraphicsPipeline();
    try {
        pipeline->init(context, &renderPasses[0], globalUniforms.getDescriptorSet(), material.get(), vertexShader, fragmentShader, config);
        // Pipeline initialized with custom config
    } catch (const std::exception& e) {
        violet::Log::error("ForwardRenderer", "Failed to initialize pipeline with custom config for shaders: '{}', '{}' - Error: {}",
            vertexShader.c_str(), fragmentShader.c_str(), e.what());
        delete pipeline;
        return nullptr;
    }

    // Validate that the pipeline was created properly
    if (!pipeline->getPipeline()) {
        violet::Log::error("ForwardRenderer", "Pipeline creation failed with custom config - null pipeline object");
        delete pipeline;
        return nullptr;
    }

    material->pipeline = pipeline;

    Material* ptr = material.get();
    materials.push_back(eastl::move(material));

    // Material created successfully with custom config
    return ptr;
}

MaterialInstance* ForwardRenderer::createMaterialInstance(Material* material) {
    // 默认创建PBR材质实例
    return createPBRMaterialInstance(material);
}

MaterialInstance* ForwardRenderer::createPBRMaterialInstance(Material* material) {
    auto instance = eastl::make_unique<PBRMaterialInstance>();
    instance->create(context, material);

    // MaterialInstance创建自己的descriptor set
    instance->createDescriptorSet(maxFramesInFlight);

    // Set default PBR textures if available
    if (defaultMetallicRoughnessTexture) {
        instance->setMetallicRoughnessTexture(defaultMetallicRoughnessTexture);
    }
    if (defaultNormalTexture) {
        instance->setNormalTexture(defaultNormalTexture);
    }

    MaterialInstance* ptr = instance.get();
    materialInstances.push_back(eastl::move(instance));

    return ptr;
}

MaterialInstance* ForwardRenderer::createUnlitMaterialInstance(Material* material) {
    if (!material) {
        violet::Log::error("ForwardRenderer", "Cannot create material instance - null material provided");
        return nullptr;
    }

    // Creating unlit material instance

    auto instance = eastl::make_unique<UnlitMaterialInstance>();

    try {
        instance->create(context, material);
        // Unlit material instance created
    } catch (const std::exception& e) {
        violet::Log::error("ForwardRenderer", "Failed to create unlit material instance: {}", e.what());
        return nullptr;
    }

    try {
        // MaterialInstance创建自己的descriptor set
        instance->createDescriptorSet(maxFramesInFlight);
        // Material instance descriptor set created
    } catch (const std::exception& e) {
        violet::Log::error("ForwardRenderer", "Failed to create descriptor set for material instance: {}", e.what());
        return nullptr;
    }

    MaterialInstance* ptr = instance.get();
    materialInstances.push_back(eastl::move(instance));

    // Unlit material instance creation completed
    return ptr;
}

void ForwardRenderer::registerMaterialInstance(uint32_t index, MaterialInstance* instance) {
    globalMaterialIndex[index] = instance;
}

MaterialInstance* ForwardRenderer::getMaterialInstanceByIndex(uint32_t index) const {
    auto it = globalMaterialIndex.find(index);
    return it != globalMaterialIndex.end() ? it->second : nullptr;
}

Texture* ForwardRenderer::addTexture(eastl::unique_ptr<Texture> texture) {
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

void GlobalUniforms::update(entt::registry& world, uint32_t frameIndex, float skyboxExposure, float skyboxRotation, bool skyboxEnabled) {
    Camera* activeCamera = findActiveCamera(world);
    if (!activeCamera) {
        violet::Log::warn("Renderer", "No active camera found!");
        return;
    }

    cachedUBO.view      = activeCamera->getViewMatrix();
    cachedUBO.proj      = activeCamera->getProjectionMatrix();
    cachedUBO.cameraPos = activeCamera->getPosition();

    // Collect lights from the scene
    cachedUBO.numLights = 0;

    // Process lights with frustum culling for point lights
    const Frustum& frustum = activeCamera->getFrustum();

    auto lightView = world.view<LightComponent, TransformComponent>();
    for (auto entity : lightView) {
        if (cachedUBO.numLights >= MAX_LIGHTS) {
            break;  // Maximum lights reached
        }

        const auto& light = lightView.get<LightComponent>(entity);
        const auto& transform = lightView.get<TransformComponent>(entity);

        if (!light.enabled) {
            continue;
        }

        // For point lights, check if within frustum
        if (light.type == LightType::Point) {
            AABB lightBounds = light.getBoundingSphere(transform.world.position);
            if (!frustum.testAABB(lightBounds)) {
                continue;  // Skip lights outside frustum
            }
        }

        uint32_t lightIndex = cachedUBO.numLights;

        // Set light position/direction based on type
        if (light.type == LightType::Directional) {
            // Store direction (not position) for directional lights
            cachedUBO.lightPositions[lightIndex] = glm::vec4(light.direction, 0.0f);  // w=0 for directional
        } else {
            // Store position for point lights
            cachedUBO.lightPositions[lightIndex] = glm::vec4(transform.world.position, 1.0f);  // w=1 for point
        }

        // Store color with intensity and radius
        glm::vec3 finalColor = light.color * light.intensity;
        cachedUBO.lightColors[lightIndex] = glm::vec4(finalColor, light.radius);

        // Store attenuation parameters
        cachedUBO.lightParams[lightIndex] = glm::vec4(
            light.linearAttenuation,
            light.quadraticAttenuation,
            0.0f, 0.0f  // Reserved for future use
        );

        cachedUBO.numLights++;
    }

    // Set ambient light (can be made configurable later)
    cachedUBO.ambientLight = glm::vec3(0.03f, 0.03f, 0.04f);  // Subtle blue-ish ambient

    // Set skybox parameters (will be configurable via UI)
    cachedUBO.skyboxExposure = skyboxExposure;
    cachedUBO.skyboxRotation = skyboxRotation;
    cachedUBO.skyboxEnabled = skyboxEnabled ? 1 : 0;

    uniformBuffers[frameIndex]->update(&cachedUBO, sizeof(cachedUBO));
    // REMOVED: descriptorSet->updateBuffer() - This was causing the UBO data to be lost!
    // The descriptor set is already bound to the buffer during initialization,
    // we only need to update the buffer contents, not rebind the descriptor set.
}

void GlobalUniforms::setSkyboxTexture(Texture* texture) {
    if (!descriptorSet) {
        violet::Log::error("Renderer", "Cannot set skybox texture - descriptor set not initialized");
        return;
    }

    if (!texture) {
        violet::Log::warn("Renderer", "Setting null skybox texture");
        return;
    }

    // Validate texture is properly initialized
    if (!texture->getImageView() || !texture->getSampler()) {
        violet::Log::error("Renderer", "Cannot set skybox texture - texture not fully initialized");
        return;
    }

    violet::Log::info("Renderer", "Setting skybox texture for {} frames", uniformBuffers.size());

    // Update all frames in flight with the same skybox texture
    for (uint32_t i = 0; i < uniformBuffers.size(); ++i) {
        descriptorSet->updateTexture(i, texture, 1); // Binding 1 for skybox texture
    }
}

void ForwardRenderer::createDefaultPBRTextures() {
    // Create basic white and black textures
    defaultWhiteTexture = addTexture(ResourceFactory::createWhiteTexture(context));
    defaultBlackTexture = addTexture(ResourceFactory::createBlackTexture(context));

    // Create default metallic-roughness texture (G=roughness=0.8, B=metallic=0.0)
    {
        auto metallicRoughnessTexture = eastl::make_unique<Texture>();
        constexpr uint32_t width = 4;
        constexpr uint32_t height = 4;
        constexpr uint32_t channels = 4;
        eastl::vector<uint8_t> pixels(width * height * channels);

        for (uint32_t i = 0; i < width * height; ++i) {
            pixels[i * 4 + 0] = 255;  // R: unused
            pixels[i * 4 + 1] = 204;  // G: roughness = 0.8 (204/255)
            pixels[i * 4 + 2] = 0;    // B: metallic = 0.0
            pixels[i * 4 + 3] = 255;  // A: alpha = 1.0
        }

        metallicRoughnessTexture->loadFromMemory(context, pixels.data(), pixels.size(), width, height, channels, false);
        defaultMetallicRoughnessTexture = addTexture(eastl::move(metallicRoughnessTexture));
    }

    // Create default normal texture (flat normal: 128,128,255)
    {
        auto normalTexture = eastl::make_unique<Texture>();
        constexpr uint32_t width = 4;
        constexpr uint32_t height = 4;
        constexpr uint32_t channels = 4;
        eastl::vector<uint8_t> pixels(width * height * channels);

        for (uint32_t i = 0; i < width * height; ++i) {
            pixels[i * 4 + 0] = 128;  // R: normal.x = 0
            pixels[i * 4 + 1] = 128;  // G: normal.y = 0
            pixels[i * 4 + 2] = 255;  // B: normal.z = 1
            pixels[i * 4 + 3] = 255;  // A: alpha = 1.0
        }

        normalTexture->loadFromMemory(context, pixels.data(), pixels.size(), width, height, channels, false);
        defaultNormalTexture = addTexture(eastl::move(normalTexture));
    }
}


void ForwardRenderer::insertPassTransition(vk::CommandBuffer cmd, size_t passIndex) {
    // Explicit pass-to-pass transitions can be added here if needed
    // For now, attachment layout transitions handle most cases
    // Future use: shadow maps, intermediate textures, etc.

    // Example for future shadow pass:
    // if (passIndex == 1 && passDescriptors[0].name == "Shadow") {
    //     RenderPass::insertMemoryBarrier(cmd,
    //         vk::PipelineStageFlagBits::eColorAttachmentOutput,
    //         vk::PipelineStageFlagBits::eFragmentShader,
    //         vk::AccessFlagBits::eColorAttachmentWrite,
    //         vk::AccessFlagBits::eShaderRead);
    // }
}

} // namespace violet