#include "Renderer.hpp"

#include "renderer/VulkanContext.hpp"
#include "renderer/Material.hpp"
#include "renderer/Mesh.hpp"
#include "renderer/Pipeline.hpp"
#include "renderer/RenderPass.hpp"
#include "renderer/DescriptorSet.hpp"
#include "renderer/UniformBuffer.hpp"
#include "renderer/Camera.hpp"
#include "ecs/Components.hpp"
#include "core/Log.hpp"

#include <EASTL/unique_ptr.h>
#include <glm/glm.hpp>

namespace violet {

Renderer::~Renderer() {
    cleanup();
}

void Renderer::init(VulkanContext* ctx, RenderPass* rp, uint32_t maxFramesInFlight) {
    context = ctx;
    renderPass = rp;
    globalUniforms.init(context, maxFramesInFlight);
}

void Renderer::cleanup() {
    globalUniforms.cleanup();
    materialInstances.clear();
    materials.clear();
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
    auto* meshComp = world.try_get<MeshComponent>(entity);

    if (!transform || !meshComp || !meshComp->mesh) {
        return;
    }

    Mesh* mesh = meshComp->mesh.get();
    glm::mat4 worldTransform = transform->world.getMatrix();

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
            matInstance = matComp->material;
        } else {
        }

        Renderable renderable(entity, mesh, matInstance ? matInstance->getMaterial() : nullptr,
                            worldTransform, static_cast<uint32_t>(i));
        renderable.visible = true;
        renderable.dirty = meshComp->dirty || transform->dirty;

        renderables.push_back(renderable);
    }

    meshComp->dirty = false;
    transform->dirty = false;
}

void Renderer::setViewport(vk::CommandBuffer commandBuffer, const vk::Extent2D& extent) {
    vk::Viewport viewport;
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
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
    Mesh* currentMesh = nullptr;


    uint32_t drawCallCount = 0;

    for (const auto& renderable : renderables) {
        if (!renderable.visible || !renderable.mesh) {
            continue;
        }

        // Material binding
        if (renderable.material != currentMaterial) {
            currentMaterial = renderable.material;
            if (currentMaterial && currentMaterial->getPipeline()) {
                currentMaterial->getPipeline()->bind(commandBuffer);

                // 绑定全局descriptor set (set 0)
                vk::DescriptorSet globalSet = globalUniforms.getDescriptorSet()->getDescriptorSet(frameIndex);
                commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                                currentMaterial->getPipelineLayout(),
                                                GLOBAL_SET, 1, &globalSet, 0, nullptr);
            } else {
                VT_WARN("Material or pipeline is null: material={}, pipeline={}",
                        currentMaterial != nullptr,
                        currentMaterial ? (currentMaterial->getPipeline() != nullptr) : false);
            }
        }

        // 绑定MaterialInstance的descriptor set (set 1)
        MaterialInstance* matInstance = nullptr;
        if (auto* matComp = world.try_get<MaterialComponent>(renderable.entity)) {
            matInstance = matComp->material;
        }

        if (matInstance && matInstance->getDescriptorSet()) {
            vk::DescriptorSet materialSet = matInstance->getDescriptorSet()->getDescriptorSet(0);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                            currentMaterial->getPipelineLayout(),
                                            MATERIAL_SET, 1, &materialSet, 0, nullptr);
        } else {
            VT_WARN("Material instance or descriptor set is null: matInstance={}, descriptorSet={}",
                    matInstance != nullptr,
                    matInstance ? (matInstance->getDescriptorSet() != nullptr) : false);
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
                VT_ERROR("Invalid vertex or index buffer: vertex={}, index={}",
                         static_cast<void*>(vertexBuffer), static_cast<void*>(indexBuffer));
                continue;
            }
        }

        // Draw call
        const SubMesh& subMesh = currentMesh->getSubMesh(renderable.subMeshIndex);

        commandBuffer.drawIndexed(subMesh.indexCount, 1, subMesh.firstIndex, 0, 0);
        drawCallCount++;
    }

}

Material* Renderer::createMaterial(const eastl::string& vertexShader, const eastl::string& fragmentShader) {

    auto material = eastl::make_unique<Material>();

    // Material自己管理descriptor set layout
    material->create(context);

    // 创建pipeline，使用global和material的descriptor set layout
    auto pipeline = new Pipeline();
    pipeline->init(context, renderPass, globalUniforms.getDescriptorSet(), material.get(), vertexShader, fragmentShader);

    material->pipeline = pipeline;

    Material* ptr = material.get();
    materials.push_back(eastl::move(material));

    VT_INFO("Material created: {} shaders", 2);
    return ptr;
}

MaterialInstance* Renderer::createMaterialInstance(Material* material) {

    auto instance = eastl::make_unique<MaterialInstance>();
    instance->create(context, material);

    // MaterialInstance创建自己的descriptor set
    instance->createDescriptorSet(1); // 暂时使用1个frame，可以改进

    MaterialInstance* ptr = instance.get();
    materialInstances.push_back(eastl::move(instance));

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
    VT_TRACE("GlobalUniforms::cleanup()");
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

    GlobalUBO ubo{};
    ubo.model = glm::mat4(1.0f);
    ubo.view = activeCamera->getViewMatrix();
    ubo.proj = activeCamera->getProjectionMatrix();

    uniformBuffers[frameIndex]->update(&ubo, sizeof(ubo));
    descriptorSet->updateBuffer(frameIndex, uniformBuffers[frameIndex].get());
}

} // namespace violet