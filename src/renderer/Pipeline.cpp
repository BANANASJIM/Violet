#include "Pipeline.hpp"
#include "VulkanContext.hpp"
#include "RenderPass.hpp"
#include "DescriptorSet.hpp"
#include "Material.hpp"
#include "Vertex.hpp"
#include "UniformBuffer.hpp"
#include "core/FileSystem.hpp"
#include "core/Log.hpp"
#include <glm/glm.hpp>

namespace violet {

void Pipeline::init(VulkanContext* ctx, RenderPass* rp, DescriptorSet* globalDescriptorSet, Material* material, const eastl::string& vertPath, const eastl::string& fragPath) {
    PipelineConfig defaultConfig;
    init(ctx, rp, globalDescriptorSet, material, vertPath, fragPath, defaultConfig);
}

void Pipeline::init(VulkanContext* ctx, RenderPass* rp, DescriptorSet* globalDescriptorSet, Material* material, const eastl::string& vertPath, const eastl::string& fragPath, const PipelineConfig& config) {
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

    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

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
    colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
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
    depthStencil.depthCompareOp = vk::CompareOp::eLess;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // 使用统一约定：global set (0) 和 material set (1)
    vk::DescriptorSetLayout setLayouts[] = {
        globalDescriptorSet->getLayout(),        // set = 0 (GLOBAL_SET)
        material->getDescriptorSetLayout()       // set = 1 (MATERIAL_SET)
    };

    // Push constant range for model matrix
    vk::PushConstantRange pushConstantRange;
    pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4); // 64 bytes for mat4

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = setLayouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

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

void Pipeline::cleanup() {
    // RAII objects will automatically clean themselves up
    // Reset in reverse order of creation
    graphicsPipeline = nullptr;
    pipelineLayout = nullptr;
    fragShaderModule = nullptr;
    vertShaderModule = nullptr;
}

eastl::vector<char> Pipeline::readFile(const eastl::string& filename) {
    auto data = FileSystem::readBinary(filename);
    if (data.empty()) {
        VT_ERROR("Failed to open file: {}", filename.c_str());
        throw std::runtime_error("Failed to open shader file");
    }

    // Convert uint8_t vector to char vector
    eastl::vector<char> buffer(data.size());
    memcpy(buffer.data(), data.data(), data.size());

    return buffer;
}

vk::raii::ShaderModule Pipeline::createShaderModule(const eastl::vector<char>& code) {
    vk::ShaderModuleCreateInfo createInfo;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    return vk::raii::ShaderModule(context->getDeviceRAII(), createInfo);
}

void Pipeline::bind(vk::CommandBuffer commandBuffer) {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline);
}

}