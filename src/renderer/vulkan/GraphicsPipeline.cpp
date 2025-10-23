#include "renderer/vulkan/GraphicsPipeline.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/DescriptorManager.hpp"
#include "renderer/vulkan/DescriptorSet.hpp"
#include "resource/Material.hpp"
#include "resource/Vertex.hpp"
#include "resource/shader/Shader.hpp"
#include "resource/shader/ReflectionHelper.hpp"
#include "core/Log.hpp"
#include <EASTL/sort.h>
#include <glm/glm.hpp>

namespace violet {

// ========================================
// Dynamic rendering init (no RenderPass dependency)
// ========================================

void GraphicsPipeline::init(VulkanContext* ctx, DescriptorManager* descMgr, Material* mat,
                            eastl::weak_ptr<Shader> vert, eastl::weak_ptr<Shader> frag,
                            const PipelineConfig& cfg) {
    context = ctx;
    descriptorManager = descMgr;
    material = mat;
    vertShader = vert;
    fragShader = frag;
    config = cfg;

    buildPipeline();
}

bool GraphicsPipeline::rebuild() {
    // Validate shader references - vertex shader required, fragment optional
    auto vert = vertShader.lock();

    if (!vert) {
        Log::error("Pipeline", "Rebuild failed: Missing vertex shader");
        return false;
    }

    Log::info("Pipeline", "Rebuilding pipeline with updated shaders");

    // Clean up old pipeline resources
    graphicsPipeline = nullptr;
    vertShaderModule = nullptr;
    fragShaderModule = nullptr;

    // Rebuild pipeline
    buildPipeline();

    Log::info("Pipeline", "Pipeline rebuild complete");
    return true;
}

void GraphicsPipeline::buildPipeline() {
    // Lock weak_ptr to get temporary shared_ptr
    auto vert = vertShader.lock();
    auto frag = fragShader.lock();

    // Vertex shader is required, fragment shader is optional (for depth-only passes)
    if (!vert) {
        Log::error("Pipeline", "buildPipeline failed: Missing vertex shader");
        return;
    }

    // Create shader modules from SPIRV
    vertShaderModule = createShaderModuleFromSPIRV(vert->getSPIRV());

    // Shader stage info
    vk::PipelineShaderStageCreateInfo vertShaderStageInfo;
    vertShaderStageInfo.stage = Shader::stageToVkFlag(vert->getStage());
    vertShaderStageInfo.module = *vertShaderModule;
    vertShaderStageInfo.pName = vert->getEntryPoint().c_str();

    eastl::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
    shaderStages.push_back(vertShaderStageInfo);

    // Fragment shader is optional (for depth-only rendering)
    if (frag) {
        fragShaderModule = createShaderModuleFromSPIRV(frag->getSPIRV());

        vk::PipelineShaderStageCreateInfo fragShaderStageInfo;
        fragShaderStageInfo.stage = Shader::stageToVkFlag(frag->getStage());
        fragShaderStageInfo.module = *fragShaderModule;
        fragShaderStageInfo.pName = frag->getEntryPoint().c_str();

        shaderStages.push_back(fragShaderStageInfo);
    }

    // Rest of pipeline creation (vertex input, assembly, viewport, etc.)
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo;

    if (config.useVertexInput) {
        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
    } else {
        vertexInputInfo.vertexBindingDescriptionCount = 0;
        vertexInputInfo.vertexAttributeDescriptionCount = 0;
    }

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
    inputAssembly.topology = config.topology;
    inputAssembly.primitiveRestartEnable = config.primitiveRestartEnable;

    vk::PipelineViewportStateCreateInfo viewportState;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    vk::PipelineRasterizationStateCreateInfo rasterizer;
    rasterizer.depthClampEnable = config.depthClampEnable;
    rasterizer.rasterizerDiscardEnable = config.rasterizerDiscardEnable;
    rasterizer.polygonMode = config.polygonMode;
    rasterizer.lineWidth = config.lineWidth;
    rasterizer.cullMode = config.cullMode;
    rasterizer.frontFace = config.frontFace;
    rasterizer.depthBiasEnable = config.depthBiasEnable;

    vk::PipelineMultisampleStateCreateInfo multisampling;
    multisampling.sampleShadingEnable = config.sampleShadingEnable;
    multisampling.rasterizationSamples = config.sampleCount;

    // Color blend state - only needed if we have color attachments
    vk::PipelineColorBlendAttachmentState colorBlendAttachment;
    vk::PipelineColorBlendStateCreateInfo colorBlending;

    if (!config.colorFormats.empty()) {
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                               vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        colorBlendAttachment.blendEnable = config.enableBlending;
        if (config.enableBlending) {
            colorBlendAttachment.srcColorBlendFactor = config.srcColorBlendFactor;
            colorBlendAttachment.dstColorBlendFactor = config.dstColorBlendFactor;
            colorBlendAttachment.colorBlendOp = config.colorBlendOp;
            colorBlendAttachment.srcAlphaBlendFactor = config.srcAlphaBlendFactor;
            colorBlendAttachment.dstAlphaBlendFactor = config.dstAlphaBlendFactor;
            colorBlendAttachment.alphaBlendOp = config.alphaBlendOp;
        }

        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = vk::LogicOp::eCopy;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
    } else {
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 0;
        colorBlending.pAttachments = nullptr;
    }

    // Dynamic states (viewport and scissor always included)
    eastl::vector<vk::DynamicState> dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };
    dynamicStates.insert(dynamicStates.end(), config.additionalDynamicStates.begin(), config.additionalDynamicStates.end());

    vk::PipelineDynamicStateCreateInfo dynamicState;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    vk::PipelineDepthStencilStateCreateInfo depthStencil;
    depthStencil.depthTestEnable = config.enableDepthTest;
    depthStencil.depthWriteEnable = config.enableDepthWrite;
    depthStencil.depthCompareOp = config.depthCompareOp;
    depthStencil.depthBoundsTestEnable = config.depthBoundsTestEnable;
    depthStencil.stencilTestEnable = config.stencilTestEnable;

    // Merge descriptor layouts and push constants from shaders
    auto merged = mergeShaderResources(vert, frag);

    Log::debug("Pipeline", "Merged {} descriptor sets and {} push constant ranges",
              merged.setLayouts.size(), merged.pushConstants.size());

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(merged.setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = merged.setLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(merged.pushConstants.size());
    pipelineLayoutInfo.pPushConstantRanges = merged.pushConstants.empty() ? nullptr : merged.pushConstants.data();

    pipelineLayout = vk::raii::PipelineLayout(context->getDeviceRAII(), pipelineLayoutInfo);

    // Dynamic rendering format info (replaces RenderPass)
    vk::PipelineRenderingCreateInfo renderingInfo;
    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(config.colorFormats.size());
    renderingInfo.pColorAttachmentFormats = config.colorFormats.data();
    renderingInfo.depthAttachmentFormat = config.depthFormat;
    renderingInfo.stencilAttachmentFormat = config.stencilFormat;

    // Graphics pipeline with dynamic rendering
    vk::GraphicsPipelineCreateInfo pipelineInfo;
    pipelineInfo.pNext = &renderingInfo;  // Chain dynamic rendering info
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = *pipelineLayout;

    graphicsPipeline = vk::raii::Pipeline(context->getDeviceRAII(), nullptr, pipelineInfo);
}

vk::raii::ShaderModule GraphicsPipeline::createShaderModuleFromSPIRV(const eastl::vector<uint32_t>& spirv) {
    vk::ShaderModuleCreateInfo createInfo;
    createInfo.codeSize = spirv.size() * sizeof(uint32_t);
    createInfo.pCode = spirv.data();
    return vk::raii::ShaderModule(context->getDeviceRAII(), createInfo);
}

void GraphicsPipeline::cleanup() {
    graphicsPipeline = nullptr;
    fragShaderModule = nullptr;
    vertShaderModule = nullptr;
    PipelineBase::cleanup();
}

void GraphicsPipeline::bind(vk::CommandBuffer commandBuffer) {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline);
}

GraphicsPipeline::MergedShaderResources GraphicsPipeline::mergeShaderResources(
    eastl::shared_ptr<Shader> vert, eastl::shared_ptr<Shader> frag) {

    MergedShaderResources result;

    if (!vert) {
        return result;  // No shaders, return empty
    }

    // Collect layout handles from both shaders (sparse vectors preserving set index)
    const auto& vertHandles = vert->getDescriptorLayoutHandles();
    const auto* fragHandles = frag ? &frag->getDescriptorLayoutHandles() : nullptr;

    // Determine max set index
    size_t maxSetIndex = vertHandles.size();
    if (fragHandles && fragHandles->size() > maxSetIndex) {
        maxSetIndex = fragHandles->size();
    }

    // Merge layouts using bit mask for quick set usage tracking
    uint32_t usedSetsMask = 0;  // Bit i = 1 if set i is used
    result.setLayouts.resize(maxSetIndex, nullptr);

    for (size_t setIndex = 0; setIndex < maxSetIndex; ++setIndex) {
        LayoutHandle vertHandle = (setIndex < vertHandles.size()) ? vertHandles[setIndex] : 0;
        LayoutHandle fragHandle = (fragHandles && setIndex < fragHandles->size()) ? (*fragHandles)[setIndex] : 0;

        if (vertHandle != 0 || fragHandle != 0) {
            // Prefer non-zero handle (they should be identical if both non-zero due to deduplication)
            LayoutHandle handle = (vertHandle != 0) ? vertHandle : fragHandle;
            result.setLayouts[setIndex] = descriptorManager->getLayout(handle);
            usedSetsMask |= (1u << setIndex);  // Mark set as used
        }
        // else: both are 0, leave nullptr in result.setLayouts[setIndex]
    }

    // Merge push constants from cached handles
    PushConstantHandle vertPC = vert->getPushConstantHandle();
    PushConstantHandle fragPC = frag ? frag->getPushConstantHandle() : 0;

    if (vertPC != 0) {
        const auto& ranges = descriptorManager->getPushConstants(vertPC);
        result.pushConstants.insert(result.pushConstants.end(), ranges.begin(), ranges.end());
    }

    if (fragPC != 0 && fragPC != vertPC) {  // Avoid duplicates if handles are same
        const auto& ranges = descriptorManager->getPushConstants(fragPC);
        result.pushConstants.insert(result.pushConstants.end(), ranges.begin(), ranges.end());
    }

    // Optional: Log which sets are used (using bit mask)
    if (usedSetsMask != 0) {
        eastl::string setsUsed;
        for (uint32_t i = 0; i < 32; ++i) {
            if (usedSetsMask & (1u << i)) {
                if (!setsUsed.empty()) setsUsed += ", ";
                char buf[8];
                sprintf(buf, "%u", i);
                setsUsed += buf;
            }
        }
        Log::debug("Pipeline", "Used descriptor sets: {}", setsUsed.c_str());
    }

    return result;
}

} // namespace violet