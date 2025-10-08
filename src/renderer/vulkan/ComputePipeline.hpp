#pragma once

#include "renderer/vulkan/PipelineBase.hpp"
#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace violet {

class VulkanContext;

struct ComputePipelineConfig {
    eastl::vector<vk::DescriptorSetLayout> descriptorSetLayouts;
    eastl::vector<vk::PushConstantRange> pushConstantRanges;
};

class ComputePipeline : public PipelineBase {
public:
    ComputePipeline() = default;
    ~ComputePipeline() override = default;

    void init(VulkanContext* context, const eastl::string& computePath,
              const ComputePipelineConfig& config = {});
    void cleanup() override;

    void bind(vk::CommandBuffer commandBuffer) override;
    vk::PipelineLayout getPipelineLayout() const override { return *pipelineLayout; }
    vk::Pipeline getPipeline() const { return *computePipeline; }

    void dispatch(vk::CommandBuffer commandBuffer, uint32_t groupCountX,
                  uint32_t groupCountY, uint32_t groupCountZ);

private:
    vk::raii::ShaderModule computeShaderModule{nullptr};
    vk::raii::Pipeline computePipeline{nullptr};
};

} // namespace violet