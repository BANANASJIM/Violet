#include "renderer/vulkan/GraphicsPipeline.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/DescriptorManager.hpp"
#include "resource/Material.hpp"
#include "resource/Vertex.hpp"
#include "resource/shader/Shader.hpp"
#include "core/Log.hpp"
#include <EASTL/sort.h>
#include <glm/glm.hpp>

namespace violet {

void GraphicsPipeline::init(VulkanContext* ctx, DescriptorManager* descMgr, Material* mat,
                            const eastl::vector<eastl::weak_ptr<Shader>>& shaderList,
                            const PipelineConfig& cfg) {
    context = ctx;
    descriptorManager = descMgr;
    material = mat;
    shaders = shaderList;
    config = cfg;

    registerDescriptorLayouts();
    buildPipeline();
}

bool GraphicsPipeline::rebuild() {
    if (shaders.empty()) {
        Log::error("Pipeline", "Rebuild failed: No shaders");
        return false;
    }

    // Validate at least one shader is still valid
    bool hasValidShader = false;
    for (const auto& weakShader : shaders) {
        if (!weakShader.expired()) {
            hasValidShader = true;
            break;
        }
    }

    if (!hasValidShader) {
        Log::error("Pipeline", "Rebuild failed: All shaders expired");
        return false;
    }

    Log::info("Pipeline", "Rebuilding pipeline with updated shaders");

    graphicsPipeline = nullptr;
    shaderModules.clear();

    buildPipeline();

    Log::info("Pipeline", "Pipeline rebuild complete");
    return true;
}

void GraphicsPipeline::buildPipeline() {
    // Lock all weak_ptrs to get shared_ptrs
    eastl::vector<eastl::shared_ptr<Shader>> shaderPtrs;
    for (const auto& weakShader : shaders) {
        auto shader = weakShader.lock();
        if (shader) {
            shaderPtrs.push_back(shader);
        }
    }

    if (shaderPtrs.empty()) {
        Log::error("Pipeline", "buildPipeline failed: No valid shaders");
        return;
    }

    // Merge shader reflection from all stages
    mergeReflection(shaderPtrs);

    // Create shader modules and stage infos
    eastl::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
    shaderModules.clear();
    shaderModules.reserve(shaderPtrs.size());

    for (auto& shader : shaderPtrs) {
        shaderModules.push_back(createShaderModuleFromSPIRV(shader->getSPIRV()));

        vk::PipelineShaderStageCreateInfo stageInfo;
        stageInfo.stage = Shader::stageToVkFlag(shader->getStage());
        stageInfo.module = *shaderModules.back();
        stageInfo.pName = (shader->getLanguage() == Shader::Language::Slang) ? "main" : shader->getEntryPoint().c_str();
        shaderStages.push_back(stageInfo);
    }

    // Vertex input
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

    // Color blend
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

    // Dynamic states
    eastl::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
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

    // Merge descriptor layouts and push constants, store handles
    auto merged = mergeShaderResources(shaderPtrs);

    // Store layout handles for getLayoutHandle()
    size_t maxSet = 0;
    for (const auto& shader : shaderPtrs) {
        if (shader->getDescriptorLayoutHandles().size() > maxSet) {
            maxSet = shader->getDescriptorLayoutHandles().size();
        }
    }
    layoutHandles.resize(maxSet, 0);
    for (size_t set = 0; set < maxSet; ++set) {
        for (const auto& shader : shaderPtrs) {
            const auto& handles = shader->getDescriptorLayoutHandles();
            if (set < handles.size() && handles[set] != 0) {
                layoutHandles[set] = handles[set];
                break;
            }
        }
    }

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(merged.setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = merged.setLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(merged.pushConstants.size());
    pipelineLayoutInfo.pPushConstantRanges = merged.pushConstants.empty() ? nullptr : merged.pushConstants.data();

    pipelineLayout = vk::raii::PipelineLayout(context->getDeviceRAII(), pipelineLayoutInfo);

    // Dynamic rendering format info
    vk::PipelineRenderingCreateInfo renderingInfo;
    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(config.colorFormats.size());
    renderingInfo.pColorAttachmentFormats = config.colorFormats.data();
    renderingInfo.depthAttachmentFormat = config.depthFormat;
    renderingInfo.stencilAttachmentFormat = config.stencilFormat;

    // Graphics pipeline
    vk::GraphicsPipelineCreateInfo pipelineInfo;
    pipelineInfo.pNext = &renderingInfo;
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
    shaderModules.clear();
    PipelineBase::cleanup();
}

void GraphicsPipeline::bind(vk::CommandBuffer commandBuffer) {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline);
}

GraphicsPipeline::MergedShaderResources GraphicsPipeline::mergeShaderResources(
    const eastl::vector<eastl::shared_ptr<Shader>>& shaderPtrs) {

    MergedShaderResources result;

    if (shaderPtrs.empty()) {
        return result;
    }

    // Determine max set index across all shaders
    size_t maxSetIndex = 0;
    for (const auto& shader : shaderPtrs) {
        const auto& handles = shader->getDescriptorLayoutHandles();
        if (handles.size() > maxSetIndex) {
            maxSetIndex = handles.size();
        }
    }

    // Merge layouts preserving set indices and store handles
    result.setLayouts.resize(maxSetIndex, nullptr);
    for (size_t setIndex = 0; setIndex < maxSetIndex; ++setIndex) {
        for (const auto& shader : shaderPtrs) {
            const auto& handles = shader->getDescriptorLayoutHandles();
            if (setIndex < handles.size() && handles[setIndex] != 0) {
                result.setLayouts[setIndex] = descriptorManager->getLayout(handles[setIndex]);
                break;
            }
        }
    }

    // Remove nullptr entries
    result.setLayouts.erase(
        eastl::remove(result.setLayouts.begin(), result.setLayouts.end(), nullptr),
        result.setLayouts.end()
    );

    // Merge push constants (deduplicate by handle)
    eastl::vector<PushConstantHandle> seenHandles;
    for (const auto& shader : shaderPtrs) {
        PushConstantHandle pcHandle = shader->getPushConstantHandle();
        if (pcHandle != 0) {
            // Check if we've already added this handle
            bool found = false;
            for (auto h : seenHandles) {
                if (h == pcHandle) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                seenHandles.push_back(pcHandle);
                const auto& ranges = descriptorManager->getPushConstants(pcHandle);
                result.pushConstants.insert(result.pushConstants.end(), ranges.begin(), ranges.end());
            }
        }
    }

    return result;
}

void GraphicsPipeline::mergeReflection(const eastl::vector<eastl::shared_ptr<Shader>>& shaderPtrs) {
    mergedReflection = eastl::make_unique<ShaderReflection>();

    // Track (set, binding) -> merged buffer index for deduplication
    eastl::unordered_map<uint64_t, size_t> mergedBufferIndices;  // (set << 32 | binding) -> index

    for (const auto& shader : shaderPtrs) {
        if (shader && shader->getShaderReflection()) {
            const ShaderReflection* refl = shader->getShaderReflection();

            // Build old index -> new index mapping for buffers
            eastl::unordered_map<size_t, size_t> indexRemap;  // old index -> new index

            // Process all resources to copy buffers and build remap
            for (const auto& resource : refl->getAllResources()) {
                if (resource.bufferLayoutIndex != ~0u) {
                    // This resource has a buffer
                    uint64_t key = (uint64_t(resource.set) << 32) | resource.binding;

                    auto it = mergedBufferIndices.find(key);
                    if (it != mergedBufferIndices.end()) {
                        // Buffer with same (set, binding) already exists, reuse it
                        indexRemap[resource.bufferLayoutIndex] = it->second;
                    } else {
                        // New buffer, copy it
                        const ReflectedBuffer* srcBuffer = refl->getBufferLayout(resource.bufferLayoutIndex);
                        if (srcBuffer) {
                            size_t newIndex = mergedReflection->storeBuffer(*srcBuffer);
                            mergedBufferIndices[key] = newIndex;
                            indexRemap[resource.bufferLayoutIndex] = newIndex;
                        }
                    }
                }
            }

            // Add all resources with remapped buffer indices
            for (auto resource : refl->getAllResources()) {
                // Remap buffer layout index if this resource has one
                if (resource.bufferLayoutIndex != ~0u) {
                    auto remapIt = indexRemap.find(resource.bufferLayoutIndex);
                    if (remapIt != indexRemap.end()) {
                        resource.bufferLayoutIndex = remapIt->second;
                    }
                }
                mergedReflection->addResource(resource);
            }

            // Add push constants
            for (const auto& pcRange : refl->getPushConstantRanges()) {
                mergedReflection->addPushConstantRange(pcRange);
            }
        }
    }
}

void GraphicsPipeline::registerDescriptorLayouts() {
    for (const auto& weakShader : shaders) {
        auto shader = weakShader.lock();
        if (shader) {
            shader->registerDescriptorLayouts(descriptorManager);
        }
    }
}

} // namespace violet