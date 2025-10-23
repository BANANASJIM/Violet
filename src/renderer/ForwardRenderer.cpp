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

    // Create materials (pipelines have descriptor layouts auto-registered from Slang reflection)
    matMgr->createPostProcessMaterial();
    matMgr->createPBRBindlessMaterial();
    matMgr->createSkyboxMaterial();

    // Create Global uniform using reflection-based API
    // Query layout handle from DescriptorManager (shaders auto-register layouts during pipeline creation)
    LayoutHandle globalLayout = descMgr.getLayoutHandle("Global");
    if (globalLayout != 0) {
        // Create per-frame uniform (triple buffering with dynamic offset)
        globalUniformHandle = descMgr.createUniform("Global", globalLayout, UpdateFrequency::PerFrame);
        violet::Log::info("Renderer", "Created Global uniform with LayoutHandle = {}", globalLayout);
    } else {
        violet::Log::error("Renderer", "Global descriptor layout not found - ensure shaders are compiled first");
    }

    tonemap.init(context, matMgr, &descMgr, renderGraph.get(), "hdr", "swapchain");

    // Initialize debug renderer (using reflection-based descriptor API)
    debugRenderer.init(context, &descMgr, resourceManager->getShaderLibrary(), framesInFlight);
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

    // Step 4: Global uniform is managed by DescriptorManager (no manual cleanup needed)
    globalUniformHandle = UniformHandle();  // Reset handle

    // Step 5: Cleanup RenderGraph
    if (renderGraph) {
        renderGraph->cleanup();
        renderGraph.reset();
    }
}


void ForwardRenderer::beginFrame(entt::registry& world, uint32_t frameIndex) {
    currentWorld = &world;

    // Set current frame for descriptor manager (enables per-frame uniform updates)
    auto& descMgr = resourceManager->getDescriptorManager();
    descMgr.setCurrentFrame(frameIndex);

    // Update auto-exposure (internal time tracking)
    autoExposure.updateExposure();

    // Pass auto-exposure EV100 to tonemap
    tonemap.setEV100(autoExposure.getCurrentEV100());

    updateGlobalUniforms(world, frameIndex);
    collectRenderables(world);

    // Update lighting and shadow systems
    if (lightingSystem && shadowSystem) {
        Camera* activeCamera = findActiveCamera(world);
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
                    UniformHandle uniform = descMgr.getUniform("Global");
                    vk::DescriptorSet globalSet = uniform.getSet();
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
    Camera* activeCamera = findActiveCamera(world);
    if (!activeCamera) {
        violet::Log::warn("Renderer", "No active camera found!");
        return;
    }

    // Get uniform handle from descriptor manager
    auto& descMgr = resourceManager->getDescriptorManager();
    UniformHandle uniform = descMgr.getUniform("Global");

    if (!uniform.isValid()) {
        violet::Log::error("Renderer", "Global uniform not initialized");
        return;
    }

    // Update uniform fields using reflection-based API
    uniform["view"] = activeCamera->getViewMatrix();
    uniform["proj"] = activeCamera->getProjectionMatrix();
    uniform["cameraPos"] = activeCamera->getPosition();

    // Initialize light data (will be populated by LightingSystem)
    uniform["numLights"] = 0;
    uniform["ambientLight"] = glm::vec3(0.03f, 0.03f, 0.04f);

    // Set skybox parameters
    uniform["skyboxExposure"] = environmentMap.getExposure();
    uniform["skyboxRotation"] = environmentMap.getRotation();
    uniform["skyboxEnabled"] = environmentMap.isEnabled() ? 1 : 0;
    uniform["iblIntensity"] = environmentMap.getIntensity();

    // Shadow parameters (will be set by ShadowSystem)
    uniform["shadowsEnabled"] = 1;  // Enable shadows by default
    uniform["cascadeDebugMode"] = 0;  // Off by default

    // Update IBL bindless indices from EnvironmentMap
    uniform["environmentMapIndex"] = environmentMap.getEnvironmentMapIndex();
    uniform["irradianceMapIndex"] = environmentMap.getIrradianceMapIndex();
    uniform["prefilteredMapIndex"] = environmentMap.getPrefilteredMapIndex();
    uniform["brdfLUTIndex"] = environmentMap.getBRDFLUTIndex();
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
    Camera* activeCamera = findActiveCamera(world);
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
    UniformHandle uniform = descMgr.getUniform("Global");
    vk::DescriptorSet globalSet = uniform.getSet();
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

// Helper function: Find active camera in the scene
Camera* ForwardRenderer::findActiveCamera(entt::registry& world) {
    auto view = world.view<CameraComponent>();
    for (auto entity : view) {
        auto& cameraComp = view.get<CameraComponent>(entity);
        if (cameraComp.isActive && cameraComp.camera) {
            return cameraComp.camera.get();
        }
    }
    return nullptr;
}



} // namespace violet