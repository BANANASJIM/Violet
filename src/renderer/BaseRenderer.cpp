#include "BaseRenderer.hpp"
#include "Mesh.hpp"
#include "VulkanContext.hpp"

namespace violet {

void BaseRenderer::bindVertexIndexBuffers(vk::CommandBuffer commandBuffer, const Mesh* mesh) {
    if (!mesh) return;

    vk::Buffer vertexBuffer = mesh->getVertexBuffer().getBuffer();
    vk::Buffer indexBuffer = mesh->getIndexBuffer().getBuffer();

    if (vertexBuffer && indexBuffer) {
        commandBuffer.bindVertexBuffers(0, vertexBuffer, {0});
        commandBuffer.bindIndexBuffer(indexBuffer, 0, mesh->getIndexBuffer().getIndexType());
    }
}

void BaseRenderer::bindGlobalDescriptors(vk::CommandBuffer commandBuffer, vk::PipelineLayout pipelineLayout,
                                        vk::DescriptorSet globalSet, uint32_t setIndex) {
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayout,
        setIndex,  // set index
        1,         // descriptor set count
        &globalSet,
        0,         // dynamic offset count
        nullptr    // dynamic offsets
    );
}

void BaseRenderer::bindMaterialDescriptors(vk::CommandBuffer commandBuffer, vk::PipelineLayout pipelineLayout,
                                          vk::DescriptorSet materialSet, uint32_t setIndex) {
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayout,
        setIndex,  // set index
        1,         // descriptor set count
        &materialSet,
        0,         // dynamic offset count
        nullptr    // dynamic offsets
    );
}

void BaseRenderer::pushModelMatrix(vk::CommandBuffer commandBuffer, vk::PipelineLayout pipelineLayout,
                                  const glm::mat4& modelMatrix) {
    struct PushConstantData {
        glm::mat4 model;
    } pushData;
    pushData.model = modelMatrix;

    commandBuffer.pushConstants(
        pipelineLayout,
        vk::ShaderStageFlagBits::eVertex,
        0,
        sizeof(PushConstantData),
        &pushData
    );
}

void BaseRenderer::setViewport(vk::CommandBuffer commandBuffer, const vk::Extent2D& extent) {
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

} // namespace violet