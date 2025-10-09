#include "renderer/vulkan/GraphicsPipeline.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/RenderPass.hpp"
#include "renderer/vulkan/DescriptorSet.hpp"
#include "resource/Material.hpp"
#include "resource/Vertex.hpp"
#include "resource/shader/Shader.hpp"
#include "core/Log.hpp"
#include <glm/glm.hpp>

namespace violet {

// ========================================
// New weak_ptr-based init
// ========================================

void GraphicsPipeline::init(VulkanContext* ctx, RenderPass* rp, Material* mat,
                            eastl::weak_ptr<Shader> vert, eastl::weak_ptr<Shader> frag,
                            const PipelineConfig& cfg) {
    context = ctx;
    renderPass = rp;
    material = mat;
    vertShader = vert;
    fragShader = frag;
    config = cfg;

    buildPipeline();
}

bool GraphicsPipeline::rebuild() {
    // Validate shader references
    auto vert = vertShader.lock();
    auto frag = fragShader.lock();

    if (!vert || !frag) {
        Log::error("Pipeline", "Rebuild failed: Shader references are invalid");
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

    if (!vert || !frag) {
        Log::error("Pipeline", "buildPipeline failed: Invalid shader references");
        return;
    }

    // Create shader modules from SPIRV
    vertShaderModule = createShaderModuleFromSPIRV(vert->getSPIRV());
    fragShaderModule = createShaderModuleFromSPIRV(frag->getSPIRV());

    // Shader stage info
    vk::PipelineShaderStageCreateInfo vertShaderStageInfo;
    vertShaderStageInfo.stage = Shader::stageToVkFlag(vert->getStage());
    vertShaderStageInfo.module = *vertShaderModule;
    vertShaderStageInfo.pName = vert->getEntryPoint().c_str();

    vk::PipelineShaderStageCreateInfo fragShaderStageInfo;
    fragShaderStageInfo.stage = Shader::stageToVkFlag(frag->getStage());
    fragShaderStageInfo.module = *fragShaderModule;
    fragShaderStageInfo.pName = frag->getEntryPoint().c_str();

    vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

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
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    vk::PipelineViewportStateCreateInfo viewportState;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    vk::PipelineRasterizationStateCreateInfo rasterizer;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = config.polygonMode;
    rasterizer.lineWidth = config.lineWidth;
    rasterizer.cullMode = config.cullMode;
    rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
    rasterizer.depthBiasEnable = VK_FALSE;

    vk::PipelineMultisampleStateCreateInfo multisampling;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineColorBlendAttachmentState colorBlendAttachment;
    colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    colorBlendAttachment.blendEnable = config.enableBlending;
    if (config.enableBlending) {
        colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
        colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
        colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
    }

    vk::PipelineColorBlendStateCreateInfo colorBlending;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = vk::LogicOp::eCopy;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    eastl::vector<vk::DynamicState> dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };
    vk::PipelineDynamicStateCreateInfo dynamicState;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    vk::PipelineDepthStencilStateCreateInfo depthStencil;
    depthStencil.depthTestEnable = config.enableDepthTest;
    depthStencil.depthWriteEnable = config.enableDepthWrite;
    depthStencil.depthCompareOp = config.depthCompareOp;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Descriptor set layouts
    eastl::vector<vk::DescriptorSetLayout> setLayouts;
    if (config.globalDescriptorSetLayout) {
        setLayouts.push_back(config.globalDescriptorSetLayout);
    }
    for (const auto& layout : config.additionalDescriptorSets) {
        setLayouts.push_back(layout);
    }
    if (config.materialDescriptorSetLayout) {
        setLayouts.push_back(config.materialDescriptorSetLayout);
    }

    // Pipeline layout
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();

    if (!config.pushConstantRanges.empty()) {
        pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(config.pushConstantRanges.size());
        pipelineLayoutInfo.pPushConstantRanges = config.pushConstantRanges.data();
    } else {
        vk::PushConstantRange pushConstantRange;
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(glm::mat4);
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    }

    pipelineLayout = vk::raii::PipelineLayout(context->getDeviceRAII(), pipelineLayoutInfo);

    // Graphics pipeline
    vk::GraphicsPipelineCreateInfo pipelineInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = *pipelineLayout;
    pipelineInfo.renderPass = renderPass->getRenderPass();
    pipelineInfo.subpass = 0;

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

} // namespace violet