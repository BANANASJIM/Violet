#include "renderer/ForwardRenderer.hpp"

#include <glm/glm.hpp>

#include <EASTL/unique_ptr.h>
#include "resource/gpu/ResourceFactory.hpp"
#include "resource/ResourceManager.hpp"

#include "core/Log.hpp"
#include "core/FileSystem.hpp"
#include "core/Timer.hpp"
#include "ui/SceneDebugLayer.hpp"
#include "ecs/Components.hpp"
#include "renderer/camera/Camera.hpp"
#include "renderer/vulkan/DescriptorSet.hpp"
#include "resource/Material.hpp"
#include "resource/Mesh.hpp"
#include "renderer/vulkan/GraphicsPipeline.hpp"
#include "renderer/graph/RenderPass.hpp"
#include "resource/gpu/UniformBuffer.hpp"
#include "renderer/vulkan/Swapchain.hpp"
#include "renderer/graph/RenderGraph.hpp"
#include "renderer/LightingSystem.hpp"
#include "renderer/ShadowSystem.hpp"
#include "renderer/ShadowPass.hpp"

namespace violet {

// Material manager access
MaterialManager* ForwardRenderer::getMaterialManager() {
    return resourceManager ? resourceManager->getMaterialManager() : nullptr;
}

const MaterialManager* ForwardRenderer::getMaterialManager() const {
    return resourceManager ? resourceManager->getMaterialManager() : nullptr;
}

DescriptorManager& ForwardRenderer::getDescriptorManager() {
    return resourceManager->getDescriptorManager();
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

    // DescriptorManager is now owned by ResourceManager and already initialized
    auto& descMgr = resourceManager->getDescriptorManager();

    // Declare all descriptor set layouts (declarative registration)
    registerDescriptorLayouts();

    // ============================================================================
    // TEMPORARY: Verify Slang shader auto-registration (TODO: Remove after testing)
    // ============================================================================
    violet::Log::info("Renderer", "=== TESTING: Verifying Slang auto-registered layouts ===");

    // Check if Slang shaders auto-registered any layouts
    // Expected pattern: "shader_name_setN" (e.g., "pbr_vert_set0")
    const char* testLayouts[] = {
        "pbr_vert_set0", "pbr_vert_set1", "pbr_vert_set2",
        "pbr_vert_set3", "pbr_vert_set4", "pbr_vert_set5"
    };

    for (const char* layoutName : testLayouts) {
        if (descMgr.hasLayout(layoutName)) {
            violet::Log::info("Renderer", "  ✓ Slang auto-registered: '{}'", layoutName);

            // Check if reflection data is stored
            auto layout = descMgr.getLayout(layoutName);
            // TODO: Add hasReflection() and getReflection() API to check field metadata
        } else {
            violet::Log::debug("Renderer", "  ✗ Layout '{}' not found (expected if using GLSL)", layoutName);
        }
    }

    violet::Log::info("Renderer", "=== END TESTING ===");
    // ============================================================================

    // Initialize subsystems
    globalUniforms.init(context, &descMgr, maxFramesInFlight);

    // Initialize RenderGraph early so it can be passed to sub-systems
    renderGraph = eastl::make_unique<RenderGraph>();
    renderGraph->init(context);


    auto* matMgr = getMaterialManager();
    if (matMgr) {
        // Set rendering formats in MaterialManager for compatible RenderPass creation
        matMgr->setRenderingFormats(swapchainFormat);

        environmentMap.init(context, matMgr, &descMgr, resourceManager->getTextureManager(), resourceManager->getShaderLibrary(), renderGraph.get());
    }

    // Initialize auto-exposure (now safe since shaders are loaded)
    autoExposure.init(context, &descMgr, currentExtent, resourceManager->getShaderLibrary(), renderGraph.get(), "hdr");

    matMgr->createPostProcessMaterial();
    matMgr->createPBRBindlessMaterial();
    matMgr->createSkyboxMaterial();

    tonemap.init(context, matMgr, &descMgr, renderGraph.get(), "hdr", "swapchain");

    // Initialize debug renderer
    debugRenderer.init(context, &globalUniforms, &descMgr, resourceManager->getShaderLibrary(), framesInFlight);
    debugRenderer.setEnabled(false);  // Disable debug renderer for testing

    // Initialize bindless through DescriptorManager
    descMgr.initBindless(1024);

    // Initialize material data SSBO for bindless architecture
    descMgr.initMaterialDataBuffer(1024);

    // TODO: Temporarily disabled shadow/lighting systems to test Slang pipeline creation
    // Initialize lighting and shadow systems
    // lightingSystem = new LightingSystem();
    // lightingSystem->init(context, &descMgr, maxFramesInFlight);

    // shadowSystem = new ShadowSystem();
    // shadowSystem->init(context, &descMgr, resourceManager->getTextureManager(), maxFramesInFlight);

    // shadowPass = eastl::make_unique<ShadowPass>();
    // shadowPass->init(context, &descMgr, resourceManager->getShaderLibrary(), shadowSystem, lightingSystem, renderGraph.get(), "shadowAtlas");

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
    shadowPass.reset();

    if (shadowSystem) {
        shadowSystem->cleanup();
        delete shadowSystem;
        shadowSystem = nullptr;
    }

    if (lightingSystem) {
        lightingSystem->cleanup();
        delete lightingSystem;
        lightingSystem = nullptr;
    }

    environmentMap.cleanup();
    tonemap.cleanup();
    debugRenderer.cleanup();

    // Step 3: Samplers are now managed by DescriptorManager (no cleanup needed)

    // Step 4: Cleanup global uniforms (may reference textures)
    globalUniforms.cleanup();

    // Step 5: Cleanup RenderGraph
    if (renderGraph) {
        renderGraph->cleanup();
        renderGraph.reset();
    }
}


void ForwardRenderer::beginFrame(entt::registry& world, uint32_t frameIndex) {
    currentWorld = &world;

    // Update auto-exposure (internal time tracking)
    autoExposure.updateExposure();

    // Pass auto-exposure EV100 to tonemap
    tonemap.setEV100(autoExposure.getCurrentEV100());

    updateGlobalUniforms(world, frameIndex);
    collectRenderables(world);

    // Update lighting and shadow systems
    if (lightingSystem && shadowSystem) {
        Camera* activeCamera = globalUniforms.findActiveCamera(world);
        if (activeCamera) {
            lightingSystem->update(world, activeCamera->getFrustum(), frameIndex);
            shadowSystem->update(world, *lightingSystem, activeCamera, frameIndex, getSceneBounds());

            lightingSystem->uploadToGPU(frameIndex);
            shadowSystem->uploadToGPU(frameIndex);
        }
    }
}

void ForwardRenderer::renderFrame(vk::CommandBuffer cmd, uint32_t imageIndex, vk::Extent2D extent, uint32_t frameIndex) {
    currentExtent = extent;
    currentFrameIndex = frameIndex;

    if (!renderGraph) {
        violet::Log::error("Renderer", "RenderGraph not initialized");
        return;
    }

    // Rebuild graph每帧 (swapchain image changes)
    rebuildRenderGraph(imageIndex);

    // Execute graph (automatic barriers + pass execution)
    renderGraph->execute(cmd, frameIndex);
}

void ForwardRenderer::endFrame() {
    currentWorld = nullptr;
}

void ForwardRenderer::rebuildRenderGraph(uint32_t imageIndex) {
    if (!swapchain || !renderGraph) {
        violet::Log::error("Renderer", "rebuildRenderGraph: swapchain or renderGraph is null");
        return;
    }

    // Clear graph every frame (reset all resource state to Undefined)
    renderGraph->clear();

    // Get swapchain image for this frame
    const ImageResource* swapchainImageRes = swapchain->getImageResource(imageIndex);
    if (!swapchainImageRes) {
        violet::Log::error("Renderer", "Failed to get swapchain ImageResource for index {}", imageIndex);
        return;
    }

    // Import swapchain image (every frame - different physical image due to triple buffering)
    // Swapchain images are pre-transitioned to PresentSrcKHR at creation (see Swapchain::transitionSwapchainImagesToPresent)
    // and must end at PresentSrcKHR (for vkQueuePresentKHR)
    renderGraph->importImage("swapchain", swapchainImageRes,
        vk::ImageLayout::ePresentSrcKHR,                         // initialLayout: pre-initialized at swapchain creation
        vk::ImageLayout::ePresentSrcKHR,                         // finalLayout: REQUIRED for vkQueuePresentKHR
        vk::PipelineStageFlagBits2::eNone,       // initialStage: ImGui renders at ColorAttachmentOutput
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,       // finalStage: FINAL TRANSITION targets ImGui stage
        {},                                                       // initialAccess: None (from vkAcquireNextImageKHR)
        {});                                                      // finalAccess: None (ImGui barrier handles dstAccess)

    // Create transient HDR render target
    vk::ClearColorValue hdrClearColor;
    hdrClearColor.setFloat32({0.0f, 0.0f, 0.0f, 1.0f});

    ImageDesc hdrDesc{
        .format = vk::Format::eR16G16B16A16Sfloat,
        .extent = {currentExtent.width, currentExtent.height, 1},
        .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
        .mipLevels = 1,
        .arrayLayers = 1,
        .clearValue = hdrClearColor
    };
    renderGraph->createImage("hdr", hdrDesc, false);

    ImageDesc depthDesc{
        .format = vk::Format::eD32Sfloat,
        .extent = {currentExtent.width, currentExtent.height, 1},
        .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
        .mipLevels = 1,
        .arrayLayers = 1,
        .clearValue = vk::ClearDepthStencilValue{1.0f, 0}
    };
    renderGraph->createImage("depth", depthDesc, false);

    // Import shadow atlas from ShadowSystem as external resource
    if (shadowSystem) {
        const ImageResource* atlasRes = shadowSystem->getAtlasImage();
        if (atlasRes && atlasRes->image) {
            renderGraph->importImage("shadowAtlas", atlasRes,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eDepthStencilReadOnlyOptimal,
                vk::PipelineStageFlagBits2::eNone,
                vk::PipelineStageFlagBits2::eFragmentShader,
                {},
                vk::AccessFlagBits2::eShaderSampledRead);
        }
    }

    // Shadow pass - render shadow maps to atlas before main pass
    if (shadowSystem && shadowSystem->getShadowCount() > 0) {
        renderGraph->addPass("Shadow", [this](RenderGraph::PassBuilder& b, RenderPass& p) {
            vk::ClearValue clearValue;
            clearValue.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

            b.write("shadowAtlas", ResourceUsage::DepthAttachment, AttachmentOptions{
                .loadOp = vk::AttachmentLoadOp::eClear,
                .storeOp = vk::AttachmentStoreOp::eStore,
                .clearValue = clearValue,
                .hasValue = true
            });

            b.execute([this](vk::CommandBuffer cmd, uint32_t frame) {
                if (shadowPass && currentWorld) {
                    shadowPass->executePass(cmd, frame, *currentWorld);
                }
            });
        });
    }

    renderGraph->addPass("Main", [this](RenderGraph::PassBuilder& b, RenderPass& p) {
        // HDR color attachment
        vk::ClearValue hdrClear;
        hdrClear.color.setFloat32({0.0f, 0.0f, 0.0f, 1.0f});
        b.write("hdr", ResourceUsage::ColorAttachment, AttachmentOptions{
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = hdrClear,
            .hasValue = true
        });

        // Depth attachment
        vk::ClearValue depthClear;
        depthClear.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
        b.write("depth", ResourceUsage::DepthAttachment, AttachmentOptions{
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = depthClear,
            .hasValue = true
        });

        // Read shadow atlas if shadows are enabled
        if (shadowSystem && shadowSystem->getShadowCount() > 0) {
            b.read("shadowAtlas", ResourceUsage::ShaderRead);
        }

        b.execute([this](vk::CommandBuffer cmd, uint32_t frame) {
            // Render skybox first as background (before scene geometry)
            if (environmentMap.isEnabled()) {
                auto* skyboxMaterial = getMaterialManager()->getMaterialByName("Skybox");
                if (skyboxMaterial && skyboxMaterial->getPipeline()) {
                    // Bind Skybox pipeline
                    skyboxMaterial->getPipeline()->bind(cmd);

                    // Rebind descriptor sets with Skybox's pipeline layout (different from PBR due to no push constants)
                    auto& descMgr = resourceManager->getDescriptorManager();
                    vk::DescriptorSet globalSet = globalUniforms.getDescriptorSet()->getDescriptorSet(frame);
                    vk::DescriptorSet bindlessSet = descMgr.getBindlessSet();

                    eastl::array<vk::DescriptorSet, 2> descriptorSets = {globalSet, bindlessSet};
                    cmd.bindDescriptorSets(
                        vk::PipelineBindPoint::eGraphics,
                        skyboxMaterial->getPipelineLayout(),
                        0,  // First set = 0
                        2,  // Bind 2 sets (Global + Bindless)
                        descriptorSets.data(),
                        0,
                        nullptr
                    );

                    // Draw fullscreen triangle (no vertex buffer needed)
                    cmd.draw(3, 1, 0, 0);
                }
            }

            // Render scene geometry after skybox
            renderScene(cmd, frame, *currentWorld);
        });
    });

    if (autoExposure.isEnabled()) {
        autoExposure.importBufferToRenderGraph(renderGraph.get());

        renderGraph->addComputePass("AutoExposure", [this](RenderGraph::PassBuilder& b, ComputePass& p) {
            b.read("hdr", ResourceUsage::ShaderRead);
            b.write(autoExposure.getBufferName(), ResourceUsage::ShaderWrite);
            b.execute([this](vk::CommandBuffer cmd, uint32_t frame) {
                autoExposure.executePass(cmd, frame);
            });
        });
    }

    renderGraph->addPass("Tonemap", [this](RenderGraph::PassBuilder& b, RenderPass& p) {
        // Read inputs
        b.read("hdr", ResourceUsage::ShaderRead);
        b.read("depth", ResourceUsage::ShaderRead);
        // Declare dependency on AutoExposure buffer to prevent pass culling
        // (even though we read EV100 via CPU, we need GPU dependency for RenderGraph)
        if (autoExposure.isEnabled()) {
            b.read(autoExposure.getBufferName(), ResourceUsage::ShaderRead);
        }

        // Swapchain as color attachment for presentation
        // Use write() with Present usage instead of writeColorAttachment to avoid duplicate accesses
        b.write("swapchain", ResourceUsage::Present);

        b.execute([this](vk::CommandBuffer cmd, uint32_t frame) {
            tonemap.executePass(cmd, frame);
        });
    });

    renderGraph->build();
    renderGraph->compile();
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
    // CRITICAL: Update IBL indices BEFORE calling update() so they're available when assembling the UBO
    globalUniforms.setIBLIndices(
        environmentMap.getEnvironmentMapIndex(),
        environmentMap.getIrradianceMapIndex(),
        environmentMap.getPrefilteredMapIndex(),
        environmentMap.getBRDFLUTIndex()
    );

    // Update global uniforms with environment map parameters
    globalUniforms.update(world, frameIndex, environmentMap.getExposure(), environmentMap.getRotation(), environmentMap.isEnabled(), environmentMap.getIntensity());
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
    auto pbrBindlessMaterial = getMaterialManager()->getMaterialByName("PBRBindless");
    if (!pbrBindlessMaterial || !pbrBindlessMaterial->getPipeline()) {
        violet::Log::error("Renderer", "PBR bindless material not available");
        return;
    }

    // Bind pipeline once for all objects
    pbrBindlessMaterial->getPipeline()->bind(commandBuffer);

    // Bind all descriptor sets once (set 0-4: Global, Bindless, MaterialData, Lighting, Shadow)
    auto& descMgr = resourceManager->getDescriptorManager();
    vk::DescriptorSet globalSet = globalUniforms.getDescriptorSet()->getDescriptorSet(frameIndex);
    vk::DescriptorSet bindlessSet = descMgr.getBindlessSet();
    vk::DescriptorSet materialDataSet = descMgr.getMaterialDataSet();
    vk::DescriptorSet lightingSet = lightingSystem ? lightingSystem->getDescriptorSet(frameIndex) : vk::DescriptorSet{};
    vk::DescriptorSet shadowSet = shadowSystem ? shadowSystem->getDescriptorSet(frameIndex) : vk::DescriptorSet{};

    eastl::array<vk::DescriptorSet, 5> descriptorSets = {globalSet, bindlessSet, materialDataSet, lightingSet, shadowSet};
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pbrBindlessMaterial->getPipelineLayout(),
        0,  // First set = 0
        5,  // Bind 5 sets
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

    // Initialize light data (will be populated by LightingSystem)
    cachedUBO.numLights = 0;
    for (int i = 0; i < 8; i++) {
        cachedUBO.lightPositions[i] = glm::vec4(0.0f);
        cachedUBO.lightColors[i] = glm::vec4(0.0f);
    }
    cachedUBO.ambientLight = glm::vec3(0.03f, 0.03f, 0.04f);

    // Set skybox parameters (will be configurable via UI)
    cachedUBO.skyboxExposure = skyboxExposure;
    cachedUBO.skyboxRotation = skyboxRotation;
    cachedUBO.skyboxEnabled = skyboxEnabled ? 1 : 0;
    cachedUBO.iblIntensity = iblIntensity;

    // Shadow parameters (will be set by ShadowSystem)
    cachedUBO.shadowsEnabled = 1;  // Enable shadows by default
    cachedUBO.cascadeDebugMode = 0;  // Off by default
    cachedUBO.padding1_0 = 0;
    cachedUBO.padding1_1 = 0;

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
        // Note: Don't upload UBO here. update() will use these member variables in the next frame.
        // Uploading cachedUBO here would overwrite other fields (view, proj, etc.) with stale values.
    }
}

void ForwardRenderer::registerDescriptorLayouts() {
    auto& descMgr = resourceManager->getDescriptorManager();

    // Global uniforms layout - per-frame updates
    // Only binding 0 (GlobalUBO) - skybox now uses bindless cubemaps
    descMgr.registerLayout({
        .name = "Global",
        .bindings = {
            {.binding = 0, .type = vk::DescriptorType::eUniformBuffer, .stages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment}
        },
        .frequency = UpdateFrequency::PerFrame
    });

    // PBR material layout - per-material updates
    descMgr.registerLayout({
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
    descMgr.registerLayout({
        .name = "UnlitMaterial",
        .bindings = {
            {.binding = 0, .type = vk::DescriptorType::eUniformBuffer, .stages = vk::ShaderStageFlagBits::eFragment},
            {.binding = 1, .type = vk::DescriptorType::eCombinedImageSampler, .stages = vk::ShaderStageFlagBits::eFragment}  // Base color
        },
        .frequency = UpdateFrequency::PerMaterial
    });

    // PostProcess layout - per-pass updates
    descMgr.registerLayout({
        .name = "PostProcess",
        .bindings = {
            {.binding = 0, .type = vk::DescriptorType::eCombinedImageSampler, .stages = vk::ShaderStageFlagBits::eFragment}, // Color texture
            {.binding = 1, .type = vk::DescriptorType::eCombinedImageSampler, .stages = vk::ShaderStageFlagBits::eFragment}  // Depth texture
        },
        .frequency = UpdateFrequency::PerPass
    });

    // Compute shader layout for equirect to cubemap
    descMgr.registerLayout({
        .name = "EquirectToCubemap",
        .bindings = {
            {.binding = 0, .type = vk::DescriptorType::eCombinedImageSampler, .stages = vk::ShaderStageFlagBits::eCompute}, // Input equirect
            {.binding = 1, .type = vk::DescriptorType::eStorageImage, .stages = vk::ShaderStageFlagBits::eCompute}           // Output cubemap
        },
        .frequency = UpdateFrequency::Static
    });

    // IBL compute shader layouts
    descMgr.registerLayout({
        .name = "IrradianceConvolution",
        .bindings = {
            {.binding = 0, .type = vk::DescriptorType::eCombinedImageSampler, .stages = vk::ShaderStageFlagBits::eCompute}, // Input environment cubemap
            {.binding = 1, .type = vk::DescriptorType::eStorageImage, .stages = vk::ShaderStageFlagBits::eCompute}           // Output irradiance cubemap
        },
        .frequency = UpdateFrequency::Static
    });

    descMgr.registerLayout({
        .name = "PrefilterEnvironment",
        .bindings = {
            {.binding = 0, .type = vk::DescriptorType::eCombinedImageSampler, .stages = vk::ShaderStageFlagBits::eCompute}, // Input environment cubemap
            {.binding = 1, .type = vk::DescriptorType::eStorageImage, .stages = vk::ShaderStageFlagBits::eCompute}           // Output prefiltered cubemap (mip level)
        },
        .frequency = UpdateFrequency::Static
    });

    descMgr.registerLayout({
        .name = "BRDFLUT",
        .bindings = {
            {.binding = 0, .type = vk::DescriptorType::eStorageImage, .stages = vk::ShaderStageFlagBits::eCompute}           // Output BRDF LUT (2D texture)
        },
        .frequency = UpdateFrequency::Static
    });

    // Bindless texture array layout - static, rarely updated
    // Binding 0: 2D textures, Binding 1: Cubemaps
    descMgr.registerLayout({
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
    descMgr.registerLayout({
        .name = "MaterialData",
        .bindings = {
            {.binding = 0, .type = vk::DescriptorType::eStorageBuffer, .stages = vk::ShaderStageFlagBits::eFragment}
        },
        .frequency = UpdateFrequency::Static
    });

    // Auto-exposure luminance compute - per-frame update
    descMgr.registerLayout({
        .name = "LuminanceCompute",
        .bindings = {
            {.binding = 0, .type = vk::DescriptorType::eCombinedImageSampler, .stages = vk::ShaderStageFlagBits::eCompute},
            {.binding = 1, .type = vk::DescriptorType::eStorageBuffer, .stages = vk::ShaderStageFlagBits::eCompute}
        },
        .frequency = UpdateFrequency::PerFrame
    });

    // Lighting system - LightData SSBO (set 3)
    descMgr.registerLayout({
        .name = "Lighting",
        .bindings = {
            {.binding = 0, .type = vk::DescriptorType::eStorageBuffer, .stages = vk::ShaderStageFlagBits::eFragment}
        },
        .frequency = UpdateFrequency::PerFrame
    });

    // Shadow system - ShadowData SSBO (set 4)
    descMgr.registerLayout({
        .name = "Shadow",
        .bindings = {
            {.binding = 0, .type = vk::DescriptorType::eStorageBuffer, .stages = vk::ShaderStageFlagBits::eFragment}
        },
        .frequency = UpdateFrequency::PerFrame
    });

    violet::Log::info("Renderer", "Registered all descriptor layouts declaratively");
}



} // namespace violet