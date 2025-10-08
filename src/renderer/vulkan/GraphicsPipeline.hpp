#pragma once

#include "renderer/vulkan/PipelineBase.hpp"
#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>

namespace violet {

class RenderPass;
class DescriptorSet;
class Material;

struct PipelineConfig {
    vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
    vk::PolygonMode polygonMode = vk::PolygonMode::eFill;
    vk::CullModeFlagBits cullMode = vk::CullModeFlagBits::eBack;
    float lineWidth = 1.0f;
    bool enableDepthTest = true;
    bool enableDepthWrite = true;
    vk::CompareOp depthCompareOp = vk::CompareOp::eLess;  // Default to less for normal depth testing
    bool enableBlending = false;
    bool useVertexInput = true;  // Set to false for full-screen effects like skybox
    eastl::vector<vk::PushConstantRange> pushConstantRanges;  // Custom push constant ranges
    eastl::vector<vk::DescriptorSetLayout> additionalDescriptorSets;  // Additional descriptor sets (e.g., bindless)
    vk::DescriptorSetLayout globalDescriptorSetLayout = nullptr;  // Global descriptor set layout from DescriptorManager
    vk::DescriptorSetLayout materialDescriptorSetLayout = nullptr;  // Material descriptor set layout from DescriptorManager
};

class GraphicsPipeline : public PipelineBase {
public:
    GraphicsPipeline() = default;
    ~GraphicsPipeline() override = default;

    // Generic init method - all configuration through PipelineConfig
    void init(VulkanContext* context, RenderPass* renderPass, Material* material,
              const eastl::string& vertPath, const eastl::string& fragPath,
              const PipelineConfig& config);

    // Convenience init with default config
    void init(VulkanContext* context, RenderPass* renderPass, Material* material,
              const eastl::string& vertPath, const eastl::string& fragPath);

    void cleanup() override;

    void bind(vk::CommandBuffer commandBuffer) override;
    vk::PipelineLayout getPipelineLayout() const override { return *pipelineLayout; }
    vk::Pipeline getPipeline() const { return *graphicsPipeline; }

private:
    vk::raii::ShaderModule vertShaderModule{nullptr};
    vk::raii::ShaderModule fragShaderModule{nullptr};
    vk::raii::Pipeline graphicsPipeline{nullptr};
};

} // namespace violet