#include "renderer/vulkan/ComputePipeline.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/DescriptorManager.hpp"
#include "resource/shader/Shader.hpp"
#include "core/Log.hpp"

namespace violet {

// New init with Shader weak_ptr
void ComputePipeline::init(VulkanContext* ctx, DescriptorManager* descMgr,
                           eastl::weak_ptr<Shader> shader,
                           const ComputePipelineConfig& cfg) {
    context = ctx;
    descriptorManager = descMgr;
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

    // Register descriptor layouts from shader reflection (idempotent - safe to call multiple times)
    if (descriptorManager) {
        shader->registerDescriptorLayouts(descriptorManager);
    }

    // Create shader module from SPIRV
    computeShaderModule = createShaderModuleFromSPIRV(shader->getSPIRV());

    // Shader stage
    vk::PipelineShaderStageCreateInfo computeShaderStageInfo;
    computeShaderStageInfo.stage = Shader::stageToVkFlag(shader->getStage());
    computeShaderStageInfo.module = *computeShaderModule;
    // Slang compiles all entry points to "main" in SPIR-V
    computeShaderStageInfo.pName = (shader->getLanguage() == Shader::Language::Slang) ? "main" : shader->getEntryPoint().c_str();

    // Get descriptor layouts from shader (automatically registered above)
    eastl::vector<vk::DescriptorSetLayout> setLayouts;
    eastl::vector<vk::PushConstantRange> pushConstants;

    // User-provided config overrides automatic extraction
    if (!config.descriptorSetLayouts.empty()) {
        setLayouts = config.descriptorSetLayouts;
    } else if (descriptorManager) {
        // Auto-extract from shader reflection
        const auto& layoutHandles = shader->getDescriptorLayoutHandles();
        setLayouts.reserve(layoutHandles.size());
        for (LayoutHandle handle : layoutHandles) {
            if (handle != 0) {
                setLayouts.push_back(descriptorManager->getLayout(handle));
            } else {
                setLayouts.push_back(nullptr);  // Sparse set - preserve index
            }
        }
    }

    // Push constants from config (required - shader doesn't provide these)
    if (!config.pushConstantRanges.empty()) {
        pushConstants = config.pushConstantRanges;
    }

    // Pipeline layout
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.empty() ? nullptr : setLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstants.size());
    pipelineLayoutInfo.pPushConstantRanges = pushConstants.empty() ? nullptr : pushConstants.data();

    pipelineLayout = vk::raii::PipelineLayout(context->getDeviceRAII(), pipelineLayoutInfo);

    violet::Log::debug("Renderer", "Compute pipeline layout: {} descriptor sets, {} push constants",
                      setLayouts.size(), pushConstants.size());

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