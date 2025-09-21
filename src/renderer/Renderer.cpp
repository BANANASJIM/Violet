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
}

void Renderer::cleanup() {
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

    const auto& subMeshes = mesh->getSubMeshes();


    for (size_t i = 0; i < subMeshes.size(); ++i) {
        const SubMesh& subMesh = subMeshes[i];
        if (!subMesh.isValid()) {
            violet::Log::warn(
                "Renderer",
                "Entity {} submesh {} is invalid (indexCount={})",
                static_cast<uint32_t>(entity),
                i,
                subMesh.indexCount
            );
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

void Renderer::renderScene(vk::CommandBuffer commandBuffer, uint32_t frameIndex, entt::registry& world) {
    Material* currentMaterial = nullptr;
    Mesh*     currentMesh     = nullptr;
    uint32_t  drawCallCount   = 0;

    for (const auto& renderable : renderables) {
        if (!renderable.visible || !renderable.mesh) {
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
