#include "renderer/ForwardRenderer.hpp"

#include <glm/glm.hpp>

#include <EASTL/unique_ptr.h>
#include <chrono>

#include "resource/gpu/ResourceFactory.hpp"
#include "resource/ResourceManager.hpp"

#include "core/Log.hpp"
#include "core/FileSystem.hpp"
#include "core/Exception.hpp"
#include "core/Timer.hpp"
#include "ui/SceneDebugLayer.hpp"
#include "ecs/Components.hpp"
#include "renderer/camera/Camera.hpp"
#include "renderer/vulkan/DescriptorSet.hpp"
#include "resource/Material.hpp"
#include "resource/Mesh.hpp"
#include "renderer/vulkan/GraphicsPipeline.hpp"
#include "renderer/vulkan/RenderPass.hpp"
#include "resource/gpu/UniformBuffer.hpp"
#include "renderer/vulkan/VulkanContext.hpp"

namespace violet {

// Material manager access
MaterialManager* ForwardRenderer::getMaterialManager() {
    return resourceManager ? &resourceManager->getMaterialManager() : nullptr;
}

const MaterialManager* ForwardRenderer::getMaterialManager() const {
    return resourceManager ? &resourceManager->getMaterialManager() : nullptr;
}

MaterialInstance* ForwardRenderer::getMaterialInstanceByIndex(uint32_t index) const {
    auto* matMgr = getMaterialManager();
    return matMgr ? const_cast<MaterialInstance*>(matMgr->getGlobalMaterial(index)) : nullptr;
}


ForwardRenderer::~ForwardRenderer() {
    cleanup();
}

void ForwardRenderer::init(VulkanContext* ctx, ResourceManager* resMgr, vk::Format swapchainFormat, uint32_t framesInFlight) {
    context = ctx;
    resourceManager = resMgr;
    maxFramesInFlight = framesInFlight;

    // Initialize descriptor manager first
    descriptorManager.init(context, maxFramesInFlight);

    // Declare all descriptor set layouts (declarative registration)
    registerDescriptorLayouts();

    // Setup multi-pass system
    setupPasses(swapchainFormat);

    // Initialize subsystems - use first graphics pass for components that need RenderPass
    globalUniforms.init(context, &descriptorManager, maxFramesInFlight);

    // Find first graphics pass for initialization
    RenderPass* firstRenderPass = getRenderPass(0);
    if (firstRenderPass) {
        debugRenderer.init(context, firstRenderPass, &globalUniforms, &descriptorManager, swapchainFormat, maxFramesInFlight);

        // Initialize EnvironmentMap with bindless architecture
        auto* matMgr = getMaterialManager();
        if (matMgr) {
            environmentMap.init(context, matMgr, &descriptorManager, &resourceManager->getTextureManager());
        }
    }

    // Initialize auto-exposure system
    autoExposure.init(context, &descriptorManager, currentExtent);

    // Initialize bindless through DescriptorManager
    descriptorManager.initBindless(1024);

    // Initialize material data SSBO for bindless architecture
    descriptorManager.initMaterialDataBuffer(1024);

    // Materials will be created later via createMaterials() after MaterialManager is initialized
}

void ForwardRenderer::createMaterials() {
    // Get PBR bindless material from MaterialManager
    auto* matMgr = getMaterialManager();
    if (matMgr) {
        pbrBindlessMaterial = matMgr->createPBRBindlessMaterial(getRenderPass(0));
    }

    // Create skybox material using MaterialManager
    RenderPass* mainPass = getRenderPass(0);
    if (mainPass && matMgr) {
        skyboxMaterial = matMgr->createSkyboxMaterial(mainPass);
    }

    // Create post-process material using MaterialManager
    RenderPass* postProcessPass = getRenderPass(1);  // PostProcess is second pass
    if (postProcessPass && matMgr) {
        // MaterialManager owns the material, we just keep a reference
        postProcessMaterial = matMgr->createPostProcessMaterial(postProcessPass);

        // Create descriptor set for post-process material
        auto sets = descriptorManager.allocateSets("PostProcess", 1);  // Only need 1 set (not per-frame)
        postProcessDescriptorSet = eastl::make_unique<DescriptorSet>();
        postProcessDescriptorSet->init(context, sets);

        // Update descriptor set with offscreen textures
        updatePostProcessDescriptors();
    }

    // Load default HDR environment map
    try {
        eastl::string hdrPath = violet::FileSystem::resolveRelativePath("assets/textures/skybox.hdr");
        violet::Log::info("Renderer", "Loading default HDR environment map: {}", hdrPath.c_str());
        environmentMap.loadHDR(hdrPath);
        environmentMap.generateIBLMaps();
        violet::Log::info("Renderer", "Default HDR environment map loaded successfully");
    } catch (const violet::Exception& e) {
        violet::Log::warn("Renderer", "Failed to load default HDR environment map: {}", e.what_c_str());
    }
}

void ForwardRenderer::cleanup() {
    // Protect against double cleanup
    if (isCleanedUp) return;
    isCleanedUp = true;

    // Step 1: Clear containers with raw pointers first
    renderables.clear();
    renderableCache.clear();

    // Step 2: Cleanup high-level rendering components
    // These may still reference materials/textures, so clean them before destroying resources
    environmentMap.cleanup();
    debugRenderer.cleanup();

    // Step 3: Cleanup render passes
    for (auto& pass : passes) {
        if (pass) {
            pass->cleanup();
        }
    }
    passes.clear();

    // Step 4: Samplers are now managed by DescriptorManager (no cleanup needed)

    // Step 5: Cleanup global uniforms (may reference textures)
    globalUniforms.cleanup();

    // Step 6: Clear material references (MaterialManager owns and cleans them)
    postProcessMaterial = nullptr;
    pbrBindlessMaterial = nullptr;
    skyboxMaterial = nullptr;

    // Step 7: Materials and textures managed by MaterialManager are cleaned in ResourceManager
    // (No cleanup needed here)

    // Step 8: Finally cleanup descriptor manager after all resources are destroyed
    descriptorManager.cleanup();  // Safe to cleanup after materials/textures are gone
}


void ForwardRenderer::beginFrame(entt::registry& world, uint32_t frameIndex) {
    currentWorld = &world;

    // Calculate deltaTime for auto-exposure
    auto currentTime = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
    lastFrameTime = currentTime;

    // Update auto-exposure (smooth interpolation)
    autoExposure.update(deltaTime);

    updateGlobalUniforms(world, frameIndex);
    collectRenderables(world);
}

void ForwardRenderer::renderFrame(vk::CommandBuffer cmd, vk::Framebuffer framebuffer, vk::Extent2D extent, uint32_t frameIndex) {
    currentExtent = extent;

    // Create sampler for auto-exposure (linear sampling)
    static vk::Sampler linearSampler = VK_NULL_HANDLE;
    if (!linearSampler) {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        linearSampler = context->getDevice().createSampler(samplerInfo);
    }

    for (size_t i = 0; i < passes.size(); ++i) {
        // Insert explicit barrier between passes if needed
        if (i > 0) {
            insertPassTransition(cmd, i);
        }

        // Execute pass
        Pass* pass = passes[i].get();

        // Handle swapchain framebuffer for graphics passes
        if (pass->getType() == PassType::Graphics) {
            RenderPass* renderPass = static_cast<RenderPass*>(pass);
            if (renderPass->getConfig().isSwapchainPass) {
                // Set external framebuffer for swapchain pass
                renderPass->setExternalFramebuffer(framebuffer);
                renderPass->begin(cmd, extent); // Will use external framebuffer
            } else {
                // Passes with own framebuffers use them directly
                renderPass->begin(cmd, extent); // Will use own framebuffer
            }
        } else {
            // Compute and other passes don't need framebuffer setup
            pass->begin(cmd, frameIndex);
        }

        pass->execute(cmd, frameIndex);
        pass->end(cmd);

        // After main pass (pass 0), compute luminance for auto-exposure
        if (i == 0 && autoExposure.getParams().enabled) {
            RenderPass* mainPass = getRenderPass(0);
            if (mainPass) {
                vk::ImageView hdrView = mainPass->getColorImageView(0);
                autoExposure.computeLuminance(cmd, hdrView, linearSampler);
            }
        }
    }
}

void ForwardRenderer::endFrame() {
    currentWorld = nullptr;
}

void ForwardRenderer::onSwapchainRecreate(vk::Extent2D newExtent) {
    currentExtent = newExtent;
    for (auto& pass : passes) {
        if (pass->getType() == PassType::Graphics) {
            static_cast<RenderPass*>(pass.get())->onSwapchainRecreate(newExtent);
        }
    }
    updatePostProcessDescriptors();
}

vk::RenderPass ForwardRenderer::getFinalPassRenderPass() const {
    // Find the last graphics pass
    for (auto it = passes.rbegin(); it != passes.rend(); ++it) {
        if ((*it)->getType() == PassType::Graphics) {
            RenderPass* renderPass = static_cast<RenderPass*>(it->get());
            return renderPass->getRenderPass();
        }
    }

    violet::Log::error("Renderer", "No graphics render passes available");
    return VK_NULL_HANDLE;
}

RenderPass* ForwardRenderer::getRenderPass(size_t index) {
    if (index >= passes.size()) {
        return nullptr;
    }

    if (passes[index]->getType() == PassType::Graphics) {
        return static_cast<RenderPass*>(passes[index].get());
    }

    return nullptr;
}

void ForwardRenderer::setupPasses(vk::Format swapchainFormat) {
    passes.clear();

    // Clear values
    vk::ClearValue colorClear;
    colorClear.color = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
    vk::ClearValue depthClear;
    depthClear.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

    // Pass 1: Main pass - Render scene to offscreen framebuffer
    RenderPassConfig mainPassConfig;
    mainPassConfig.name = "Main";
    mainPassConfig.colorAttachments = {AttachmentDesc::color(swapchainFormat, vk::AttachmentLoadOp::eClear)};
    mainPassConfig.depthAttachment = AttachmentDesc::depth(context->findDepthFormat(), vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore);
    mainPassConfig.hasDepth = true;
    mainPassConfig.clearValues = {colorClear, depthClear};
    mainPassConfig.isSwapchainPass = false;  // Render to offscreen, not swapchain
    mainPassConfig.createOwnFramebuffer = true;  // Create own offscreen framebuffer
    mainPassConfig.followsSwapchainSize = true;  // Size follows swapchain
    mainPassConfig.srcStage = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
    mainPassConfig.dstStage = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
    mainPassConfig.srcAccess = {};
    mainPassConfig.dstAccess = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    mainPassConfig.execute = [this](vk::CommandBuffer cmd, uint32_t frame) {
        if (currentWorld) {
            this->setViewport(cmd, currentExtent);

            // Render skybox first (if enabled and material exists)
            if (environmentMap.isEnabled() && skyboxMaterial && skyboxMaterial->getPipeline()) {
                skyboxMaterial->getPipeline()->bind(cmd);
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                    skyboxMaterial->getPipelineLayout(), 0,
                    globalUniforms.getDescriptorSet()->getDescriptorSet(frame), {});
                // Bindless descriptor set for cubemap array
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                    skyboxMaterial->getPipelineLayout(), 1,
                    descriptorManager.getBindlessSet(), {});
                cmd.draw(3, 1, 0, 0); // Full-screen triangle
            }

            // Then render scene geometry (depth testing enabled)
            renderScene(cmd, frame, *currentWorld);
        }
    };

    auto mainPass = eastl::make_unique<RenderPass>();
    mainPass->init(context, mainPassConfig);

    // Create offscreen framebuffers
    if (mainPassConfig.createOwnFramebuffer) {
        mainPass->createFramebuffers(currentExtent);
    }

    passes.push_back(eastl::move(mainPass));

    // Pass 2: PostProcess pass - Render fullscreen quad to swapchain
    RenderPassConfig postProcessConfig;
    postProcessConfig.name = "PostProcess";
    postProcessConfig.colorAttachments = {AttachmentDesc::swapchainColor(swapchainFormat, vk::AttachmentLoadOp::eClear)};
    postProcessConfig.depthAttachment = AttachmentDesc::swapchainDepth(context->findDepthFormat(), vk::AttachmentLoadOp::eClear);
    postProcessConfig.hasDepth = true;  // Need depth for debug renderer compatibility with swapchain framebuffer
    postProcessConfig.clearValues = {colorClear, depthClear};
    postProcessConfig.isSwapchainPass = true;  // Render to swapchain
    postProcessConfig.createOwnFramebuffer = false;  // Use external swapchain framebuffer
    postProcessConfig.srcStage = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
    postProcessConfig.dstStage = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
    postProcessConfig.srcAccess = {};
    postProcessConfig.dstAccess = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    postProcessConfig.execute = [this](vk::CommandBuffer cmd, uint32_t frame) {
        this->setViewport(cmd, currentExtent);

        if (!postProcessMaterial || !postProcessMaterial->getPipeline()) {
            return;
        }

        // Bind post-process pipeline
        postProcessMaterial->getPipeline()->bind(cmd);

        // Bind descriptor set with offscreen textures (set 1)
        if (postProcessDescriptorSet) {
            vk::DescriptorSet descSet = postProcessDescriptorSet->getDescriptorSet(0);
            cmd.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                postProcessMaterial->getPipelineLayout(),
                1,  // MATERIAL_SET = 1
                1,
                &descSet,
                0,
                nullptr
            );
        }

        // Push constants for tone mapping parameters (EV100 + gamma)
        struct PostProcessParams {
            float ev100;
            float gamma;
        } params;
        // Use auto-exposure EV100 if enabled, otherwise use manual value
        params.ev100 = autoExposure.getCurrentEV100();
        params.gamma = postProcessGamma;

        cmd.pushConstants(
            postProcessMaterial->getPipelineLayout(),
            vk::ShaderStageFlagBits::eFragment,
            0,
            sizeof(PostProcessParams),
            &params
        );

        // Draw fullscreen quad (3 vertices, no vertex buffer)
        cmd.draw(3, 1, 0, 0);
    };

    auto postProcessPass = eastl::make_unique<RenderPass>();
    postProcessPass->init(context, postProcessConfig);

    passes.push_back(eastl::move(postProcessPass));
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
    // Update global uniforms with environment map parameters
    globalUniforms.update(world, frameIndex, environmentMap.getExposure(), environmentMap.getRotation(), environmentMap.isEnabled(), environmentMap.getIntensity());

    // Update IBL bindless indices in global uniforms
    globalUniforms.setIBLIndices(
        environmentMap.getEnvironmentMapIndex(),
        environmentMap.getIrradianceMapIndex(),
        environmentMap.getPrefilteredMapIndex(),
        environmentMap.getBRDFLUTIndex()
    );
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
                    // Fallback to first submesh bounds
                    if (!meshComp->subMeshWorldBounds.empty()) {
                        renderableBounds.push_back(meshComp->subMeshWorldBounds[0]);
                    } else {
                        renderableBounds.push_back(AABB{});  // Empty bounds
                    }
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

    // ========== BINDLESS RENDERING ==========
    if (!pbrBindlessMaterial || !pbrBindlessMaterial->getPipeline()) {
        violet::Log::error("Renderer", "PBR bindless material not available");
        return;
    }

    // Bind pipeline once for all objects
    pbrBindlessMaterial->getPipeline()->bind(commandBuffer);

    // Bind all descriptor sets once (set 0: Global, set 1: Bindless Textures, set 2: Material Data SSBO)
    vk::DescriptorSet globalSet = globalUniforms.getDescriptorSet()->getDescriptorSet(frameIndex);
    vk::DescriptorSet bindlessSet = descriptorManager.getBindlessSet();
    vk::DescriptorSet materialDataSet = descriptorManager.getMaterialDataSet();

    eastl::array<vk::DescriptorSet, 3> descriptorSets = {globalSet, bindlessSet, materialDataSet};
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pbrBindlessMaterial->getPipelineLayout(),
        0,  // First set = 0
        3,  // Bind 3 sets
        descriptorSets.data(),
        0,
        nullptr
    );

    Mesh* currentMesh = nullptr;

    // Render loop - only update push constants per object
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

        // Bind vertex/index buffers if mesh changed
        if (renderable.mesh != currentMesh) {
            currentMesh = renderable.mesh;
            this->bindVertexIndexBuffers(commandBuffer, currentMesh);
        }

        // Get material instance to retrieve materialID
        MaterialInstance* matInstance = nullptr;
        if (auto* matComp = world.try_get<MaterialComponent>(renderable.entity)) {
            const SubMesh& subMesh = currentMesh->getSubMesh(renderable.subMeshIndex);
            uint32_t materialId = matComp->getMaterialId(subMesh.materialIndex);
            matInstance = getMaterialInstanceByIndex(materialId);
        }

        if (!matInstance) {
            renderStats.skippedRenderables++;
            continue;
        }

        // Push constants: model matrix + material ID
        BindlessPushConstants push{
            .model = renderable.worldTransform,
            .materialID = matInstance->getMaterialID(),
            .padding = {0, 0, 0}
        };

        commandBuffer.pushConstants(
            pbrBindlessMaterial->getPipelineLayout(),
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0,
            sizeof(BindlessPushConstants),
            &push
        );

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
                            // Fallback to first submesh bounds if something is wrong
                            if (!meshComp->subMeshWorldBounds.empty()) {
                                aabbs.push_back(meshComp->subMeshWorldBounds[0]);
                            } else {
                                aabbs.push_back(AABB{});  // Empty bounds
                            }
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

// All material creation methods removed - use MaterialManager instead

// GlobalUniforms implementation
GlobalUniforms::~GlobalUniforms() {
    cleanup();
}

void GlobalUniforms::init(VulkanContext* ctx, DescriptorManager* descMgr, uint32_t maxFramesInFlight) {
    context = ctx;

    // Allocate descriptor sets from DescriptorManager
    auto sets = descMgr->allocateSets("Global", maxFramesInFlight);
    descriptorSet = eastl::make_unique<DescriptorSet>();
    descriptorSet->init(context, sets);

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

void GlobalUniforms::update(entt::registry& world, uint32_t frameIndex, float skyboxExposure, float skyboxRotation, bool skyboxEnabled, float iblIntensity) {
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

        // Store color with intensity (physical units: lux for directional, lumens for point) and radius
        glm::vec3 finalColor = light.color * light.intensity;
        cachedUBO.lightColors[lightIndex] = glm::vec4(finalColor, light.radius);

        cachedUBO.numLights++;
    }

    // Set ambient light (can be made configurable later)
    cachedUBO.ambientLight = glm::vec3(0.03f, 0.03f, 0.04f);  // Subtle blue-ish ambient

    // Set skybox parameters (will be configurable via UI)
    cachedUBO.skyboxExposure = skyboxExposure;
    cachedUBO.skyboxRotation = skyboxRotation;
    cachedUBO.skyboxEnabled = skyboxEnabled ? 1 : 0;
    cachedUBO.iblIntensity = iblIntensity;

    // Update IBL bindless indices
    cachedUBO.environmentMapIndex = iblEnvironmentMapIndex;
    cachedUBO.irradianceMapIndex = iblIrradianceMapIndex;
    cachedUBO.prefilteredMapIndex = iblPrefilteredMapIndex;
    cachedUBO.brdfLUTIndex = iblBRDFLUTIndex;

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

void GlobalUniforms::setIBLIndices(uint32_t envMap, uint32_t irradiance, uint32_t prefiltered, uint32_t brdfLUT) {
    // Only log if values actually changed
    bool changed = (iblEnvironmentMapIndex != envMap ||
                    iblIrradianceMapIndex != irradiance ||
                    iblPrefilteredMapIndex != prefiltered ||
                    iblBRDFLUTIndex != brdfLUT);

    iblEnvironmentMapIndex = envMap;
    iblIrradianceMapIndex = irradiance;
    iblPrefilteredMapIndex = prefiltered;
    iblBRDFLUTIndex = brdfLUT;

    if (changed) {
        violet::Log::info("Renderer", "IBL indices set - Env: {}, Irradiance: {}, Prefiltered: {}, BRDF: {}",
                         envMap, irradiance, prefiltered, brdfLUT);
    }
}

void ForwardRenderer::registerDescriptorLayouts() {
    // Global uniforms layout - per-frame updates
    descriptorManager.registerLayout({
        .name = "Global",
        .bindings = {
            {.binding = 0, .type = vk::DescriptorType::eUniformBuffer, .stages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
            {.binding = 1, .type = vk::DescriptorType::eCombinedImageSampler, .stages = vk::ShaderStageFlagBits::eFragment}
        },
        .frequency = UpdateFrequency::PerFrame
    });

    // PBR material layout - per-material updates
    descriptorManager.registerLayout({
        .name = "PBRMaterial",
        .bindings = {
            {.binding = 0, .type = vk::DescriptorType::eUniformBuffer, .stages = vk::ShaderStageFlagBits::eFragment},
            {.binding = 1, .type = vk::DescriptorType::eCombinedImageSampler, .stages = vk::ShaderStageFlagBits::eFragment}, // Base color
            {.binding = 2, .type = vk::DescriptorType::eCombinedImageSampler, .stages = vk::ShaderStageFlagBits::eFragment}, // Metallic-roughness
            {.binding = 3, .type = vk::DescriptorType::eCombinedImageSampler, .stages = vk::ShaderStageFlagBits::eFragment}, // Normal
            {.binding = 4, .type = vk::DescriptorType::eCombinedImageSampler, .stages = vk::ShaderStageFlagBits::eFragment}, // Occlusion
            {.binding = 5, .type = vk::DescriptorType::eCombinedImageSampler, .stages = vk::ShaderStageFlagBits::eFragment}  // Emissive
        },
        .frequency = UpdateFrequency::PerMaterial
    });

    // Unlit material layout - per-material updates
    descriptorManager.registerLayout({
        .name = "UnlitMaterial",
        .bindings = {
            {.binding = 0, .type = vk::DescriptorType::eUniformBuffer, .stages = vk::ShaderStageFlagBits::eFragment},
            {.binding = 1, .type = vk::DescriptorType::eCombinedImageSampler, .stages = vk::ShaderStageFlagBits::eFragment}  // Base color
        },
        .frequency = UpdateFrequency::PerMaterial
    });

    // PostProcess layout - per-pass updates
    descriptorManager.registerLayout({
        .name = "PostProcess",
        .bindings = {
            {.binding = 0, .type = vk::DescriptorType::eCombinedImageSampler, .stages = vk::ShaderStageFlagBits::eFragment}, // Color texture
            {.binding = 1, .type = vk::DescriptorType::eCombinedImageSampler, .stages = vk::ShaderStageFlagBits::eFragment}  // Depth texture
        },
        .frequency = UpdateFrequency::PerPass
    });

    // Compute shader layout for equirect to cubemap
    descriptorManager.registerLayout({
        .name = "EquirectToCubemap",
        .bindings = {
            {.binding = 0, .type = vk::DescriptorType::eCombinedImageSampler, .stages = vk::ShaderStageFlagBits::eCompute}, // Input equirect
            {.binding = 1, .type = vk::DescriptorType::eStorageImage, .stages = vk::ShaderStageFlagBits::eCompute}           // Output cubemap
        },
        .frequency = UpdateFrequency::Static
    });

    // IBL compute shader layouts
    descriptorManager.registerLayout({
        .name = "IrradianceConvolution",
        .bindings = {
            {.binding = 0, .type = vk::DescriptorType::eCombinedImageSampler, .stages = vk::ShaderStageFlagBits::eCompute}, // Input environment cubemap
            {.binding = 1, .type = vk::DescriptorType::eStorageImage, .stages = vk::ShaderStageFlagBits::eCompute}           // Output irradiance cubemap
        },
        .frequency = UpdateFrequency::Static
    });

    descriptorManager.registerLayout({
        .name = "PrefilterEnvironment",
        .bindings = {
            {.binding = 0, .type = vk::DescriptorType::eCombinedImageSampler, .stages = vk::ShaderStageFlagBits::eCompute}, // Input environment cubemap
            {.binding = 1, .type = vk::DescriptorType::eStorageImage, .stages = vk::ShaderStageFlagBits::eCompute}           // Output prefiltered cubemap (mip level)
        },
        .frequency = UpdateFrequency::Static
    });

    descriptorManager.registerLayout({
        .name = "BRDFLUT",
        .bindings = {
            {.binding = 0, .type = vk::DescriptorType::eStorageImage, .stages = vk::ShaderStageFlagBits::eCompute}           // Output BRDF LUT (2D texture)
        },
        .frequency = UpdateFrequency::Static
    });

    // Bindless texture array layout - static, rarely updated
    // Binding 0: 2D textures, Binding 1: Cubemaps
    descriptorManager.registerLayout({
        .name = "Bindless",
        .bindings = {
            {.binding = 0, .type = vk::DescriptorType::eCombinedImageSampler, .stages = vk::ShaderStageFlagBits::eFragment, .count = 1024},  // 2D textures
            {.binding = 1, .type = vk::DescriptorType::eCombinedImageSampler, .stages = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eVertex, .count = 64}    // Cubemaps
        },
        .frequency = UpdateFrequency::Static,
        .flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
        .bindingFlags = vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eUpdateAfterBind
    });

    // Material data SSBO - bindless architecture (set 2)
    // Contains all material parameters + texture indices
    descriptorManager.registerLayout({
        .name = "MaterialData",
        .bindings = {
            {.binding = 0, .type = vk::DescriptorType::eStorageBuffer, .stages = vk::ShaderStageFlagBits::eFragment}
        },
        .frequency = UpdateFrequency::Static
    });

    // Auto-exposure luminance compute - per-frame update
    descriptorManager.registerLayout({
        .name = "LuminanceCompute",
        .bindings = {
            {.binding = 0, .type = vk::DescriptorType::eCombinedImageSampler, .stages = vk::ShaderStageFlagBits::eCompute},
            {.binding = 1, .type = vk::DescriptorType::eStorageBuffer, .stages = vk::ShaderStageFlagBits::eCompute}
        },
        .frequency = UpdateFrequency::PerFrame
    });

    violet::Log::info("Renderer", "Registered all descriptor layouts declaratively");
}

void ForwardRenderer::updatePostProcessDescriptors() {
    if (!postProcessDescriptorSet || passes.size() < 2) {
        return;
    }

    RenderPass* mainPass = getRenderPass(0);
    if (!mainPass) {
        return;
    }

    // Get offscreen textures from Main pass
    vk::ImageView colorView = mainPass->getColorImageView(0);
    vk::ImageView depthView = mainPass->getDepthImageView();

    if (!colorView || !depthView) {
        violet::Log::warn("Renderer", "Failed to get offscreen textures for post-process");
        return;
    }

    // Get sampler from DescriptorManager (reuse cached sampler)
    vk::Sampler sampler = descriptorManager.getSampler(SamplerType::ClampToEdge);

    // Update descriptor set with color texture (binding 0)
    vk::DescriptorImageInfo colorImageInfo{};
    colorImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    colorImageInfo.imageView = colorView;
    colorImageInfo.sampler = sampler;

    vk::WriteDescriptorSet colorWrite{};
    colorWrite.dstSet = postProcessDescriptorSet->getDescriptorSet(0);
    colorWrite.dstBinding = 0;
    colorWrite.dstArrayElement = 0;
    colorWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    colorWrite.descriptorCount = 1;
    colorWrite.pImageInfo = &colorImageInfo;

    // Update descriptor set with depth texture (binding 1)
    vk::DescriptorImageInfo depthImageInfo{};
    depthImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    depthImageInfo.imageView = depthView;
    depthImageInfo.sampler = sampler;

    vk::WriteDescriptorSet depthWrite{};
    depthWrite.dstSet = postProcessDescriptorSet->getDescriptorSet(0);
    depthWrite.dstBinding = 1;
    depthWrite.dstArrayElement = 0;
    depthWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    depthWrite.descriptorCount = 1;
    depthWrite.pImageInfo = &depthImageInfo;

    eastl::array<vk::WriteDescriptorSet, 2> writes = {colorWrite, depthWrite};
    context->getDevice().updateDescriptorSets(2, writes.data(), 0, nullptr);
}

void ForwardRenderer::insertPassTransition(vk::CommandBuffer cmd, size_t passIndex) {
    // Transition from Main pass (index 0) to PostProcess pass (index 1)
    if (passIndex == 1 && passes.size() >= 2) {
        RenderPass* mainPass = getRenderPass(0);
        if (!mainPass) return;

        // Get offscreen color and depth images
        vk::Image colorImage = mainPass->getColorImage(0);
        vk::Image depthImage = mainPass->getDepthImage();

        // Transition color attachment from write to read
        if (colorImage) {
            vk::ImageMemoryBarrier colorBarrier{};
            colorBarrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
            colorBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            colorBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            colorBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            colorBarrier.image = colorImage;
            colorBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            colorBarrier.subresourceRange.baseMipLevel = 0;
            colorBarrier.subresourceRange.levelCount = 1;
            colorBarrier.subresourceRange.baseArrayLayer = 0;
            colorBarrier.subresourceRange.layerCount = 1;
            colorBarrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            colorBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eColorAttachmentOutput,
                vk::PipelineStageFlagBits::eFragmentShader,
                {},
                {},
                {},
                colorBarrier
            );
        }

        // Transition depth attachment from write to read
        if (depthImage) {
            vk::ImageMemoryBarrier depthBarrier{};
            depthBarrier.oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            depthBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            depthBarrier.image = depthImage;
            depthBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
            depthBarrier.subresourceRange.baseMipLevel = 0;
            depthBarrier.subresourceRange.levelCount = 1;
            depthBarrier.subresourceRange.baseArrayLayer = 0;
            depthBarrier.subresourceRange.layerCount = 1;
            depthBarrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            depthBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eLateFragmentTests,
                vk::PipelineStageFlagBits::eFragmentShader,
                {},
                {},
                {},
                depthBarrier
            );
        }
    }
}

} // namespace violet