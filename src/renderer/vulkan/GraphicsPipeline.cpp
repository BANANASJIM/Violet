#include "renderer/vulkan/GraphicsPipeline.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/RenderPass.hpp"
#include "renderer/vulkan/DescriptorSet.hpp"
#include "resource/Material.hpp"
#include "resource/Vertex.hpp"
#include "core/Log.hpp"
#include <glm/glm.hpp>

namespace violet {

void GraphicsPipeline::init(VulkanContext* ctx, RenderPass* rp, Material* material,
                            const eastl::string& vertPath, const eastl::string& fragPath) {
    PipelineConfig defaultConfig;
    init(ctx, rp, material, vertPath, fragPath, defaultConfig);
}

void GraphicsPipeline::init(VulkanContext* ctx, RenderPass* rp, Material* material,
                            const eastl::string& vertPath, const eastl::string& fragPath,
                            const PipelineConfig& config) {
    context = ctx;

    auto vertShaderCode = readFile(vertPath);
    auto fragShaderCode = readFile(fragPath);

    vertShaderModule = createShaderModule(vertShaderCode);
    fragShaderModule = createShaderModule(fragShaderCode);

    vk::PipelineShaderStageCreateInfo vertShaderStageInfo;
    vertShaderStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
    vertShaderStageInfo.module = *vertShaderModule;
    vertShaderStageInfo.pName = "main";

    vk::PipelineShaderStageCreateInfo fragShaderStageInfo;
    fragShaderStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
    fragShaderStageInfo.module = *fragShaderModule;
    fragShaderStageInfo.pName = "main";

    vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo;

    if (config.useVertexInput) {
        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
    } else {
        // No vertex input for full-screen effects like skybox
        vertexInputInfo.vertexBindingDescriptionCount = 0;
        vertexInputInfo.vertexAttributeDescriptionCount = 0;
        vertexInputInfo.pVertexBindingDescriptions = nullptr;
        vertexInputInfo.pVertexAttributeDescriptions = nullptr;
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
    depthStencil.depthCompareOp = config.depthCompareOp;  // Use config's depth compare op
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    eastl::vector<vk::DescriptorSetLayout> setLayouts;

    // Add global descriptor set layout from config
    if (config.globalDescriptorSetLayout) {
        setLayouts.push_back(config.globalDescriptorSetLayout);
    } else {
        violet::Log::error("Renderer", "GraphicsPipeline: No global descriptor set layout provided in config");
    }

    // Add additional descriptor sets from config (e.g., bindless texture array)
    for (const auto& layout : config.additionalDescriptorSets) {
        setLayouts.push_back(layout);
    }

    // Add material descriptor set layout if provided in config
    if (config.materialDescriptorSetLayout) {
        setLayouts.push_back(config.materialDescriptorSetLayout);
    }

    // Use push constant ranges from config if specified
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();

    if (!config.pushConstantRanges.empty()) {
        // Use custom push constant ranges from config
        pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(config.pushConstantRanges.size());
        pipelineLayoutInfo.pPushConstantRanges = config.pushConstantRanges.data();
    } else {
        // Default push constant range (model matrix only)
        vk::PushConstantRange pushConstantRange;
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(glm::mat4);

        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    }

    pipelineLayout = vk::raii::PipelineLayout(context->getDeviceRAII(), pipelineLayoutInfo);

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
    pipelineInfo.renderPass = rp->getRenderPass();
    pipelineInfo.subpass = 0;

    graphicsPipeline = vk::raii::Pipeline(context->getDeviceRAII(), nullptr, pipelineInfo);
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