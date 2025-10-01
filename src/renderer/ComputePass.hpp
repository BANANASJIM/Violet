#pragma once

#include "Pass.hpp"
#include "ComputePipeline.hpp"
#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>

namespace violet {

class VulkanContext;
class DescriptorSet;

// Resource barrier configuration for automatic image/buffer layout management
struct ResourceBarrier {
    vk::Image image = VK_NULL_HANDLE;
    vk::ImageLayout oldLayout = vk::ImageLayout::eUndefined;
    vk::ImageLayout newLayout = vk::ImageLayout::eGeneral;
    vk::ImageSubresourceRange subresourceRange{
        vk::ImageAspectFlagBits::eColor,  // aspectMask
        0,  // baseMipLevel
        1,  // levelCount
        0,  // baseArrayLayer
        1   // layerCount
    };
    vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
    vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eComputeShader;
    vk::AccessFlags srcAccess = {};
    vk::AccessFlags dstAccess = vk::AccessFlagBits::eShaderWrite;
};

// Compute pass configuration
struct ComputePassConfig : public PassConfigBase {
    // Compute-specific configuration
    eastl::vector<vk::DescriptorSetLayout> descriptorSetLayouts;
    eastl::vector<vk::PushConstantRange> pushConstantRanges;
    eastl::string shaderPath;

    // Resource barriers (automatic layout management)
    eastl::vector<ResourceBarrier> preBarriers;   // Barriers before execution
    eastl::vector<ResourceBarrier> postBarriers;  // Barriers after execution

    // Default constructor
    ComputePassConfig() {
        type = PassType::Compute;
        srcStage = vk::PipelineStageFlagBits::eComputeShader;
        dstStage = vk::PipelineStageFlagBits::eComputeShader;
    }
};

// Compute pass implementation
class ComputePass : public Pass {
public:
    ComputePass();
    ~ComputePass() override;

    // Initialization
    void init(VulkanContext* context, const ComputePassConfig& config);
    void cleanup() override;

    // Pass interface implementation
    void begin(vk::CommandBuffer cmd, uint32_t frameIndex) override;
    void execute(vk::CommandBuffer cmd, uint32_t frameIndex) override;
    void end(vk::CommandBuffer cmd) override;

    PassType getType() const override { return PassType::Compute; }
    const eastl::string& getName() const override { return config.name; }

    // Barrier configuration access
    vk::PipelineStageFlags getSrcStage() const override { return config.srcStage; }
    vk::PipelineStageFlags getDstStage() const override { return config.dstStage; }
    vk::AccessFlags getSrcAccess() const override { return config.srcAccess; }
    vk::AccessFlags getDstAccess() const override { return config.dstAccess; }

    // Compute-specific API
    ComputePipeline* getPipeline() { return pipeline.get(); }
    const ComputePassConfig& getConfig() const { return config; }

private:
    void insertPreBarriers(vk::CommandBuffer cmd);
    void insertPostBarriers(vk::CommandBuffer cmd);

private:
    VulkanContext* context = nullptr;
    ComputePassConfig config;
    eastl::unique_ptr<ComputePipeline> pipeline;
};

} // namespace violet
