#include "ComputePipeline.hpp"
#include "VulkanContext.hpp"
#include "core/Log.hpp"

namespace violet {

void ComputePipeline::init(VulkanContext* ctx, const eastl::string& computePath,
                           const ComputePipelineConfig& config) {
    context = ctx;

    auto computeShaderCode = readFile(computePath);
    computeShaderModule = createShaderModule(computeShaderCode);

    vk::PipelineShaderStageCreateInfo computeShaderStageInfo;
    computeShaderStageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    computeShaderStageInfo.module = *computeShaderModule;
    computeShaderStageInfo.pName = "main";

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(config.descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = config.descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(config.pushConstantRanges.size());
    pipelineLayoutInfo.pPushConstantRanges = config.pushConstantRanges.data();

    pipelineLayout = vk::raii::PipelineLayout(context->getDeviceRAII(), pipelineLayoutInfo);

    vk::ComputePipelineCreateInfo computePipelineCreateInfo;
    computePipelineCreateInfo.stage = computeShaderStageInfo;
    computePipelineCreateInfo.layout = *pipelineLayout;

    computePipeline = vk::raii::Pipeline(context->getDeviceRAII(), nullptr, computePipelineCreateInfo);

    violet::Log::info("Renderer", "Compute pipeline created successfully from: {}", computePath.c_str());
}

void ComputePipeline::cleanup() {
    computePipeline = nullptr;
    computeShaderModule = nullptr;
    PipelineBase::cleanup();
}

void ComputePipeline::bind(vk::CommandBuffer commandBuffer) {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *computePipeline);
}

void ComputePipeline::dispatch(vk::CommandBuffer commandBuffer, uint32_t groupCountX,
                               uint32_t groupCountY, uint32_t groupCountZ) {
    commandBuffer.dispatch(groupCountX, groupCountY, groupCountZ);
}

} // namespace violet