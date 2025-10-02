#pragma once

#include <vulkan/vulkan_raii.hpp>
#include <EASTL/vector.h>
#include <glm/glm.hpp>
#include <entt/entt.hpp>

namespace violet {

class VulkanContext;
class RenderPass;
class Mesh;
class Material;
class Pipeline;

// Abstract base class for all renderer types
class BaseRenderer {
public:
    BaseRenderer() = default;
    virtual ~BaseRenderer() = default;


    // Common public methods
    void setViewport(vk::CommandBuffer commandBuffer, const vk::Extent2D& extent);

protected:
    // Common renderer state
    VulkanContext* context = nullptr;
    RenderPass* renderPass = nullptr;
    uint32_t maxFramesInFlight = 0;

    // Common utility functions for binding operations
    void bindVertexIndexBuffers(vk::CommandBuffer commandBuffer, const Mesh* mesh);
    void bindGlobalDescriptors(vk::CommandBuffer commandBuffer, vk::PipelineLayout pipelineLayout,
                              vk::DescriptorSet globalSet, uint32_t setIndex = 0);
    void bindMaterialDescriptors(vk::CommandBuffer commandBuffer, vk::PipelineLayout pipelineLayout,
                                vk::DescriptorSet materialSet, uint32_t setIndex = 1);
    void pushModelMatrix(vk::CommandBuffer commandBuffer, vk::PipelineLayout pipelineLayout,
                        const glm::mat4& modelMatrix);
};

} // namespace violet