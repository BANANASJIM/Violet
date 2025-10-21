#include "ShadowPass.hpp"
#include "ShadowSystem.hpp"
#include "LightingSystem.hpp"
#include "Renderable.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/GraphicsPipeline.hpp"
#include "renderer/graph/RenderGraph.hpp"
#include "resource/shader/ShaderLibrary.hpp"
#include "resource/Mesh.hpp"
#include "core/Log.hpp"
#include <glm/glm.hpp>

namespace violet {

void ShadowPass::init(VulkanContext* ctx, ShaderLibrary* shaderLib,
                      ShadowSystem* shadowSys, LightingSystem* lightingSys,
                      RenderGraph* graph, const eastl::string& atlasName) {
    context = ctx;
    shaderLibrary = shaderLib;
    shadowSystem = shadowSys;
    lightingSystem = lightingSys;
    renderGraph = graph;
    atlasImageName = atlasName;

    auto shadowVert = shaderLibrary->get("shadow_vert");
    if (shadowVert.expired()) {
        violet::Log::error("ShadowPass", "Failed to load shadow.vert shader");
        return;
    }

    PipelineConfig config{};
    config.enableDepthTest = true;
    config.enableDepthWrite = true;
    config.depthCompareOp = vk::CompareOp::eLess;
    config.cullMode = vk::CullModeFlagBits::eNone;  // No culling for shadow pass

    // Dynamic rendering - depth-only pass (no color attachments)
    config.colorFormats = {};  // Empty - depth-only
    config.depthFormat = vk::Format::eD32Sfloat;
    config.stencilFormat = vk::Format::eUndefined;

    vk::PushConstantRange pushConstRange;
    pushConstRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
    pushConstRange.offset = 0;
    pushConstRange.size = sizeof(glm::mat4) * 2;
    config.pushConstantRanges.push_back(pushConstRange);

    shadowPipeline = eastl::make_unique<GraphicsPipeline>();
    shadowPipeline->init(context, nullptr, shadowVert, eastl::weak_ptr<Shader>(), config);

    violet::Log::info("ShadowPass", "Initialized shadow pass");
}

void ShadowPass::cleanup() {
    shadowPipeline.reset();
}

void ShadowPass::executePass(vk::CommandBuffer cmd, uint32_t frameIndex, entt::registry& world) {
    if (!shadowPipeline || !shadowSystem || !lightingSystem) {
        return;
    }

    // Get shadow renderables from ShadowSystem (not camera-culled)
    const auto& renderables = shadowSystem->getShadowRenderables();

    const auto& shadowData = shadowSystem->getShadowData();
    if (shadowData.empty()) {
        return;
    }

    uint32_t atlasSize = shadowSystem->getAtlasSize();

    // Bind shadow pipeline once
    shadowPipeline->bind(cmd);

    for (size_t i = 0; i < shadowData.size(); i++) {
        const auto& shadow = shadowData[i];

        // For each cascade in this shadow
        uint32_t numCascades = shadow.cascadeCount;
        for (uint32_t c = 0; c < numCascades; c++) {
            // Set viewport and scissor for this cascade's shadow map region
            vk::Viewport viewport;
            viewport.x = shadow.atlasRects[c].x * atlasSize;
            viewport.y = shadow.atlasRects[c].y * atlasSize;
            viewport.width = shadow.atlasRects[c].z * atlasSize;
            viewport.height = shadow.atlasRects[c].w * atlasSize;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            cmd.setViewport(0, 1, &viewport);

            vk::Rect2D scissor;
            scissor.offset.x = static_cast<int32_t>(viewport.x);
            scissor.offset.y = static_cast<int32_t>(viewport.y);
            scissor.extent.width = static_cast<uint32_t>(viewport.width);
            scissor.extent.height = static_cast<uint32_t>(viewport.height);
            cmd.setScissor(0, 1, &scissor);

            // Render all objects from this cascade's perspective
            Mesh* currentMesh = nullptr;

            for (const auto& renderable : renderables) {
                if (!renderable.mesh || !renderable.visible) continue;

                // Bind vertex and index buffers if mesh changed
                if (currentMesh != renderable.mesh) {
                    currentMesh = renderable.mesh;

                    vk::Buffer vertexBuffer = currentMesh->getVertexBuffer().getBuffer();
                    vk::DeviceSize offset = 0;
                    cmd.bindVertexBuffers(0, 1, &vertexBuffer, &offset);
                    cmd.bindIndexBuffer(currentMesh->getIndexBuffer().getBuffer(), 0,
                                       currentMesh->getIndexBuffer().getIndexType());
                }

                const SubMesh& subMesh = renderable.mesh->getSubMesh(renderable.subMeshIndex);

                // Push constants: light space matrix + model matrix
                struct ShadowPushConstants {
                    glm::mat4 lightSpaceMatrix;
                    glm::mat4 model;
                } push;

                push.lightSpaceMatrix = shadow.cascadeViewProjMatrices[c];
                push.model = renderable.worldTransform;

            cmd.pushConstants(
                shadowPipeline->getPipelineLayout(),
                vk::ShaderStageFlagBits::eVertex,
                0,
                sizeof(ShadowPushConstants),
                &push
            );

            cmd.drawIndexed(subMesh.indexCount, 1, subMesh.firstIndex, 0, 0);
            }
        }
    }
}

} // namespace violet