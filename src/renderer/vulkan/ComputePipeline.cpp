#include "renderer/vulkan/ComputePipeline.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "resource/shader/Shader.hpp"
#include "core/Log.hpp"

namespace violet {

// New init with Shader weak_ptr
void ComputePipeline::init(VulkanContext* ctx, eastl::weak_ptr<Shader> shader,
                           const ComputePipelineConfig& cfg) {
    context = ctx;
    computeShader = shader;
    config = cfg;

    buildPipeline();
}

bool ComputePipeline::rebuild() {
    auto shader = computeShader.lock();
    if (!shader) {
        violet::Log::error("Renderer", "Cannot rebuild compute pipeline: shader reference expired");
        return false;
    }

    // Clean up old pipeline resources
    computePipeline = nullptr;
    computeShaderModule = nullptr;

    // Rebuild with current shader
    buildPipeline();

    violet::Log::info("Renderer", "Compute pipeline rebuilt successfully: {}", shader->getName().c_str());
    return true;
}

void ComputePipeline::buildPipeline() {
    auto shader = computeShader.lock();
    if (!shader) {
        violet::Log::error("Renderer", "Cannot build compute pipeline: shader reference expired");
        return;
    }

    // Create shader module from SPIRV
    computeShaderModule = createShaderModuleFromSPIRV(shader->getSPIRV());

    // Shader stage
    vk::PipelineShaderStageCreateInfo computeShaderStageInfo;
    computeShaderStageInfo.stage = Shader::stageToVkFlag(shader->getStage());
    computeShaderStageInfo.module = *computeShaderModule;
    computeShaderStageInfo.pName = shader->getEntryPoint().c_str();

    // Pipeline layout
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(config.descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = config.descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(config.pushConstantRanges.size());
    pipelineLayoutInfo.pPushConstantRanges = config.pushConstantRanges.data();

    pipelineLayout = vk::raii::PipelineLayout(context->getDeviceRAII(), pipelineLayoutInfo);

    // Create compute pipeline
    vk::ComputePipelineCreateInfo computePipelineCreateInfo;
    computePipelineCreateInfo.stage = computeShaderStageInfo;
    computePipelineCreateInfo.layout = *pipelineLayout;

    // Use createComputePipeline to avoid ambiguity with graphics pipeline constructor
    computePipeline = context->getDeviceRAII().createComputePipeline(nullptr, computePipelineCreateInfo);

    violet::Log::debug("Renderer", "Compute pipeline created: {}", shader->getName().c_str());
}

vk::raii::ShaderModule ComputePipeline::createShaderModuleFromSPIRV(const eastl::vector<uint32_t>& spirv) {
    vk::ShaderModuleCreateInfo createInfo;
    createInfo.codeSize = spirv.size() * sizeof(uint32_t);
    createInfo.pCode = spirv.data();

    return vk::raii::ShaderModule(context->getDeviceRAII(), createInfo);
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